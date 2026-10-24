/*
  ============================================================================
  QRcade — Firmware CAFETERA v1.0.0
  ============================================================================
  Primera versión para placas tipo "cafetera" (cobro directo con Mercado
  Pago, sin pantalla). Combina DOS cosas que ya estaban probadas por separado
  y que NUNCA se tocan entre sí:

    A) El esqueleto de red/blindaje de la garra (qrcade_v2.0.2.ino):
       watchdog de hardware, persistencia manual de WiFi, portal de
       configuración SOLO bajo demanda, check-in por socket TLS crudo con
       timeouts duros, reinicios preventivos, OTA. Se copia tal cual, sólo
       se le sacó todo lo de pantalla/QR (esta placa no tiene pantalla).

    B) La lógica real de la cafetera (reconstruida en esp32_mpay_replica.ino
       a partir del análisis de la placa ya soldada): pines de relé/pulso/
       calentador, limpieza automática y manual, reset remoto por BLE, y el
       polling directo a la API de Mercado Pago desde la ESP32 (a pedido
       explícito del dueño, esto se queda en la placa — no se mueve a un
       webhook del servidor).

  LO QUE CAMBIA respecto a la réplica original: el portal cautivo de la
  placa (red "QRcade_Config") AHORA SOLO configura WiFi. Ya no tiene los
  campos de ID Comercio / ID Disp / Token / tiempos — todo eso ahora llega
  desde nuestro propio backend (panel qrcade.dev) en cada check-in, dentro
  del campo "cafe_config" de la respuesta, y se cachea en NVS propia para
  que la máquina siga funcionando con el último valor conocido si el
  backend está momentáneamente inalcanzable (el mismo criterio que usa la
  garra con el QR).

  REGLA DE ORO DE ESTA PLACA: si algo de lo que llega del backend es
  inválido/falta, se usan los últimos valores cacheados (o los de fábrica
  si todavía no hay ninguno) — nunca se deja el relé en un estado indefinido
  ni se bloquea el loop esperando al servidor.

  Pines (soldados, NO se pueden cambiar sin resoldar la placa):
    - GPIO25: habilitación del relé / matriz de botones de la cafetera.
              HIGH = activo (habilitado), LOW = inactivo. Mismo pin que ya
              usaba el firmware original.
    - GPIO27: entrada, sensa el pulso de "fin de ciclo" que manda la
              cafetera sola cuando termina de servir.
    - GPIO26: candidato a relé del calentador. SIN CONFIRMAR FÍSICAMENTE
              (ver TODO más abajo) — probar con multímetro antes de confiar
              en esta lógica en una máquina real.
    - GPIO0 (BOOT): mantenido al arrancar (≥2s) → portal de config WiFi.
              Mantenido durante operación normal (≥4s) → borrado de fábrica
              completo (WiFi + toda la config cacheada) y reinicio. Mismo
              mecanismo de "boton de emergencia" que ya tenía el firmware
              original, se conserva tal cual.

  Librerías necesarias (Arduino Library Manager):
    - WiFiManager (tzapu)
    - ArduinoJson (bblanchon)
    - Preferences / BLEDevice / BLEServer / BLEUtils / HTTPClient / HTTPUpdate
      (todas incluidas en el core ESP32, no hace falta instalar nada aparte)

  Partition Scheme en Arduino IDE: a diferencia de la garra, este firmware
  pesa más por la librería BLE — con "Default 4MB with spiffs" (1.25MB por
  partición de app) NO entra ("text section exceeds available space").
  Usar Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP with OTA)":
  sigue teniendo dos particiones de app (OTA funciona), con 1.9MB cada una
  entra con margen. "Huge APP" tampoco sirve (una sola partición, sin OTA).
  ============================================================================
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <HTTPUpdate.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============================================================================
//                          CONFIG DE FÁBRICA (por tanda)
// ============================================================================
// MISMA clave que usan las garras — se valida contra el único
// app_settings.factory_key_hash que hay en Supabase, sin importar el "kind"
// de la placa.
static const char *FACTORY_KEY = "6d250dc5d880fe75c824a4c9eb64520cca1d270961ae17f9";

static const char *BACKEND_HOST = "qrcade.dev";
static const uint16_t BACKEND_PORT = 443;
static const char *CHECKIN_PATH = "/api/device/checkin";

static const char *FIRMWARE_VERSION = "cafe-1.0.0";

// ============================================================================
//                          CONFIG DE HARDWARE (pines soldados)
// ============================================================================
#define PIN_RELAY_HABILITA   25
#define PIN_PULSO_FIN_CICLO  27
#define PIN_CALENTADOR       26   // TODO: NO CONFIRMADO — probar con multímetro (candidatos: 26, 32, 33)

static const int CONFIG_BUTTON_PIN = 0;                       // BOOT
static const unsigned long CONFIG_BUTTON_HOLD_MS = 2000;      // mantenido AL ARRANCAR -> portal WiFi
static const unsigned long FACTORY_RESET_HOLD_MS = 4000;      // mantenido EN OPERACIÓN -> borrado de fábrica

// ============================================================================
//                          BLE (Nordic UART Service) — reset remoto
// ============================================================================
#define NUS_SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_RX_UUID   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_TX_UUID   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *pTxChar = nullptr;
bool bleClienteConectado = false;

// ============================================================================
//                          BLINDAJE / HARDENING (igual que la garra)
// ============================================================================
static const bool DEBUG_LOG = true;

static const uint32_t WDT_TIMEOUT_S = 30;
static const uint32_t WDT_TIMEOUT_PORTAL_S = 90;

static const unsigned long WIFI_DOWN_RESTART_MS = 5UL * 60UL * 1000UL;        // 5 min
static const unsigned long RESTART_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL; // 12 h
static const uint32_t HEAP_SAFETY_THRESHOLD = 20000;                          // bytes

// No hay pantalla que refrescar y van a ser 21+ placas pegándole al mismo
// backend — un poco más relajado que los 5s de la garra.
static const unsigned long CHECKIN_INTERVAL_MS = 15000;

// ============================================================================
//                          ESTADO DE CHECK-IN / BACKEND
// ============================================================================
Preferences preferences; // scratch temporal, se usa begin/end puntual
unsigned long lastCheckinAt = 0;
unsigned long lastWifiOkAt = 0;
unsigned long bootAt = 0;

String deviceStatus = "";  // "unclaimed" | "claimed" | "revoked" | "" (sin respuesta aún)
String claimCode = "";
bool devicePaused = false;

uint32_t checkinOkCount = 0;
uint32_t checkinFailCount = 0;

// ============================================================================
//            CONFIG OPERATIVA DE LA CAFETERA (cafe_config del check-in)
// ============================================================================
// Mismos campos que antes se tipeaban a mano en el portal cautivo del
// firmware original — ahora llegan del backend y se cachean en NVS propia
// (namespace "cafecfg") para seguir funcionando con el último valor
// conocido si el backend está momentáneamente inalcanzable.
String cfgMpAccessToken = "";
String cfgMpCollectorId = "";
String cfgMpExternalRef = "";
unsigned long cfgPulsoMinUs   = 50000;
unsigned long cfgOffDelayMs   = 500;
unsigned long cfgCleanPulseMs = 3000;
int  cfgHeatOnMin  = 20;
int  cfgHeatOffMin = 5;
int  cfgCleanEveryNCycles = 50;
String cfgBtResetCmd = "RESET847200";
bool cfgPulsoNivelHigh = true;

bool cafeConfigCargada = false; // true en cuanto hay algo (cacheado o fresco) para operar

static const char *CAFE_PREFS_NS = "cafecfg";

// ============================================================================
//            ESTADO DE PAGOS / CICLO DE SERVIDO / LIMPIEZA
// ============================================================================
String lastPaymentId = "";
uint32_t usedCredits = 0;
unsigned long lastMpCheckMs = 0;
static const unsigned long MP_CHECK_INTERVAL_MS = 60000; // 1 consulta a MP por minuto

bool relayActivo = false;
bool esperandoFinDeCiclo = false;

unsigned long heaterLastToggleMs = 0;
bool heaterOn = false;

bool actualizacionFirmwareEnCurso = false;

// ============================================================================
//                    WIFIMANAGER (sólo bajo demanda, SOLO WiFi)
// ============================================================================
WiFiManager wm;

// ============================================================================
//                    PERSISTENCIA MANUAL DE CREDENCIALES WIFI
// ============================================================================
// Idéntico a la garra: la persistencia nativa de WiFiManager/driver resultó
// poco confiable con hardware real (la placa se olvida el WiFi tras un
// apagado/encendido real sin esto). Guardamos nosotros mismos SSID/pass en
// un namespace propio de Preferences y los usamos como única fuente de
// verdad para reconectar al arrancar.
Preferences wifiPrefs;
static const char *WIFI_PREFS_NS = "wifi";
static const char *WIFI_PREFS_KEY_SSID = "ssid";
static const char *WIFI_PREFS_KEY_PASS = "pass";

void guardarCredencialesWifi(const String &ssid, const String &pass) {
  if (ssid.length() == 0) {
    logLine("!! guardarCredencialesWifi: SSID vacio, no se guarda nada.");
    return;
  }
  wifiPrefs.begin(WIFI_PREFS_NS, false);
  wifiPrefs.putString(WIFI_PREFS_KEY_SSID, ssid);
  wifiPrefs.putString(WIFI_PREFS_KEY_PASS, pass);
  wifiPrefs.end();
  logLine(">> Credenciales WiFi guardadas manualmente en Preferences (SSID: " + ssid + ").");
}

bool leerCredencialesWifi(String &ssidOut, String &passOut) {
  wifiPrefs.begin(WIFI_PREFS_NS, true);
  ssidOut = wifiPrefs.getString(WIFI_PREFS_KEY_SSID, "");
  passOut = wifiPrefs.getString(WIFI_PREFS_KEY_PASS, "");
  wifiPrefs.end();
  return ssidOut.length() > 0;
}

// ============================================================================
//                          UTILIDADES
// ============================================================================
void logLine(const String &s) {
  if (DEBUG_LOG) Serial.println(s);
}

// Para delays largos que SÍ hay que hacer (limpieza, espera de pulso de fin
// de ciclo) sin dejar que el watchdog de 30s se dispare por el medio.
void delayConWdt(unsigned long ms) {
  unsigned long inicio = millis();
  while (millis() - inicio < ms) {
    esp_task_wdt_reset();
    delay(20);
  }
}

// ============================================================================
//            CACHE EN NVS DE LA CONFIG OPERATIVA (cafe_config)
// ============================================================================
void configurarPinPulso() {
  pinMode(PIN_PULSO_FIN_CICLO, cfgPulsoNivelHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
}

void cargarCafeConfigCacheada() {
  preferences.begin(CAFE_PREFS_NS, true); // sólo lectura
  cfgMpAccessToken     = preferences.getString("mp_token", "");
  cfgMpCollectorId     = preferences.getString("mp_collector", "");
  cfgMpExternalRef     = preferences.getString("mp_extref", "");
  cfgPulsoMinUs        = preferences.getULong("pulso_us", cfgPulsoMinUs);
  cfgOffDelayMs        = preferences.getULong("off_delay", cfgOffDelayMs);
  cfgCleanPulseMs      = preferences.getULong("clean_ms", cfgCleanPulseMs);
  cfgHeatOnMin         = preferences.getInt("heat_on", cfgHeatOnMin);
  cfgHeatOffMin        = preferences.getInt("heat_off", cfgHeatOffMin);
  cfgCleanEveryNCycles = preferences.getInt("clean_n", cfgCleanEveryNCycles);
  cfgBtResetCmd        = preferences.getString("bt_cmd", cfgBtResetCmd);
  cfgPulsoNivelHigh    = preferences.getBool("pulso_hi", cfgPulsoNivelHigh);
  lastPaymentId        = preferences.getString("last_id", "");
  usedCredits          = preferences.getUInt("used_cred", 0);
  bool habiaAlgoCacheado = preferences.getBool("tiene_cfg", false);
  preferences.end();

  configurarPinPulso();
  if (habiaAlgoCacheado) {
    cafeConfigCargada = true;
    logLine(">> Config de cafetera cacheada encontrada en NVS, usandola hasta el proximo check-in.");
  } else {
    logLine(">> Sin config cacheada todavia — usando defaults de fabrica hasta el primer check-in OK.");
  }
}

// Vuelca a NVS la config que acaba de llegar del backend, para que
// sobreviva un reinicio/apagado aunque el backend quede inalcanzable.
void guardarCafeConfigCacheada() {
  preferences.begin(CAFE_PREFS_NS, false);
  preferences.putString("mp_token", cfgMpAccessToken);
  preferences.putString("mp_collector", cfgMpCollectorId);
  preferences.putString("mp_extref", cfgMpExternalRef);
  preferences.putULong("pulso_us", cfgPulsoMinUs);
  preferences.putULong("off_delay", cfgOffDelayMs);
  preferences.putULong("clean_ms", cfgCleanPulseMs);
  preferences.putInt("heat_on", cfgHeatOnMin);
  preferences.putInt("heat_off", cfgHeatOffMin);
  preferences.putInt("clean_n", cfgCleanEveryNCycles);
  preferences.putString("bt_cmd", cfgBtResetCmd);
  preferences.putBool("pulso_hi", cfgPulsoNivelHigh);
  preferences.putBool("tiene_cfg", true);
  preferences.end();
}

void guardarUsedCredits() {
  preferences.begin(CAFE_PREFS_NS, false);
  preferences.putUInt("used_cred", usedCredits);
  preferences.end();
}

void guardarLastPaymentId(const String &id) {
  preferences.begin(CAFE_PREFS_NS, false);
  preferences.putString("last_id", id);
  preferences.end();
}

// Aplica el objeto "cafe_config" tal como vino en la respuesta del
// check-in. Cualquier campo que falte se deja con el valor que ya había
// (cacheado o default) en vez de pisarlo con un 0/"" — nunca confiamos a
// ciegas en que el backend mandó todo completo.
void aplicarCafeConfig(JsonObject cfg) {
  if (cfg["mp_access_token"].is<const char *>()) cfgMpAccessToken = String((const char *)cfg["mp_access_token"]);
  if (cfg["mp_collector_id"].is<const char *>()) cfgMpCollectorId = String((const char *)cfg["mp_collector_id"]);
  if (cfg["mp_external_reference"].is<const char *>()) cfgMpExternalRef = String((const char *)cfg["mp_external_reference"]);
  if (!cfg["pulso_min_us"].isNull()) cfgPulsoMinUs = cfg["pulso_min_us"].as<unsigned long>();
  if (!cfg["off_delay_ms"].isNull()) cfgOffDelayMs = cfg["off_delay_ms"].as<unsigned long>();
  if (!cfg["clean_pulse_ms"].isNull()) cfgCleanPulseMs = cfg["clean_pulse_ms"].as<unsigned long>();
  if (!cfg["heat_on_min"].isNull()) cfgHeatOnMin = cfg["heat_on_min"].as<int>();
  if (!cfg["heat_off_min"].isNull()) cfgHeatOffMin = cfg["heat_off_min"].as<int>();
  if (!cfg["clean_every_n_cycles"].isNull()) cfgCleanEveryNCycles = cfg["clean_every_n_cycles"].as<int>();
  if (cfg["bt_reset_cmd"].is<const char *>()) cfgBtResetCmd = String((const char *)cfg["bt_reset_cmd"]);
  if (!cfg["pulso_nivel_high"].isNull()) cfgPulsoNivelHigh = cfg["pulso_nivel_high"].as<bool>();

  configurarPinPulso();
  guardarCafeConfigCacheada();
  cafeConfigCargada = true;
}

// ============================================================================
//            BLE — Nordic UART Service, reset remoto (igual que antes)
// ============================================================================
void borrarWifiYConfigCompleta() {
  logLine(">> Borrando WiFi + config de cafetera cacheada por completo...");
  wm.resetSettings();
  wifiPrefs.begin(WIFI_PREFS_NS, false);
  wifiPrefs.clear();
  wifiPrefs.end();
  preferences.begin(CAFE_PREFS_NS, false);
  preferences.clear();
  preferences.end();
  logLine(">> Reiniciando...");
  delay(300);
  ESP.restart();
}

class RxCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String recibido = String(pChar->getValue().c_str());
    logLine("[BLE] Recibido: " + recibido);
    if (recibido == cfgBtResetCmd) {
      logLine("[BLE] Comando correcto. Reset de fabrica solicitado.");
      pTxChar->setValue("OK_RESET");
      pTxChar->notify();
      delay(200);
      borrarWifiYConfigCompleta();
    } else {
      logLine("[BLE] Comando incorrecto.");
      pTxChar->setValue("ERROR_CMD");
      pTxChar->notify();
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    bleClienteConectado = true;
    logLine("[BLE] Cliente conectado.");
  }
  void onDisconnect(BLEServer *pServer) override {
    bleClienteConectado = false;
    logLine("[BLE] Cliente desconectado, reiniciando advertising.");
    pServer->getAdvertising()->start();
  }
};

void iniciarBLE(const String &nombreDispositivo) {
  logLine("[BLE] Iniciando (" + nombreDispositivo + ")...");
  BLEDevice::init(nombreDispositivo.c_str());
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(NUS_SERVICE_UUID);

  pTxChar = pService->createCharacteristic(NUS_CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTxChar->addDescriptor(new BLE2902());

  BLECharacteristic *pRxChar = pService->createCharacteristic(NUS_CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  pRxChar->setCallbacks(new RxCallback());

  pService->start();
  pServer->getAdvertising()->start();
  logLine("[BLE] Listo y visible.");
}

// ============================================================================
//            RELÉ DE HABILITACIÓN (GPIO25) + SENSADO FIN DE CICLO (GPIO27)
// ============================================================================
void activarRelay() {
  digitalWrite(PIN_RELAY_HABILITA, HIGH);
  relayActivo = true;
  esperandoFinDeCiclo = true;
  logLine(">> Relay ACTIVADO — esperando pulso de fin de ciclo en GPIO27...");
}

void desactivarRelayConRetardo() {
  if (cfgOffDelayMs > 0) {
    delayConWdt(cfgOffDelayMs);
  }
  digitalWrite(PIN_RELAY_HABILITA, LOW);
  relayActivo = false;
  logLine(">> Relay DESACTIVADO.");
}

// Sensa un pulso válido en GPIO27 (bloqueante con timeout + watchdog
// alimentado durante toda la espera — nunca puede colgar la placa).
bool sensarPulsoFinDeCiclo(unsigned long timeoutMs) {
  int nivelActivo = cfgPulsoNivelHigh ? HIGH : LOW;
  unsigned long inicioEspera = millis();

  while (digitalRead(PIN_PULSO_FIN_CICLO) != nivelActivo) {
    if (millis() - inicioEspera > timeoutMs) return false;
    esp_task_wdt_reset();
    delay(5);
  }
  unsigned long inicioPulso = micros();

  while (digitalRead(PIN_PULSO_FIN_CICLO) == nivelActivo) {
    if ((millis() - inicioEspera) > timeoutMs) return false;
    esp_task_wdt_reset();
  }
  unsigned long duracionUs = micros() - inicioPulso;

  if (duracionUs >= cfgPulsoMinUs) {
    logLine(">> Pulso valido en GPIO27 — ciclo terminado (" + String(duracionUs) + "us).");
    return true;
  }
  logLine(">> Pulso en GPIO27 ignorado, duracion insuficiente (" + String(duracionUs) + "us).");
  return false;
}

// Dos pulsos de cfgCleanPulseMs separados por 20s — igual que el firmware
// original. Se dispara sola cada cfgCleanEveryNCycles créditos, o a pedido
// manual desde la app (comando "clean_cycle").
void ejecutarCicloDeLimpieza() {
  logLine(">> Iniciando ciclo de limpieza...");

  digitalWrite(PIN_RELAY_HABILITA, HIGH);
  delayConWdt(cfgCleanPulseMs);
  digitalWrite(PIN_RELAY_HABILITA, LOW);
  logLine(">> Limpieza: fin primer pulso, esperando 20s...");
  delayConWdt(20000);

  digitalWrite(PIN_RELAY_HABILITA, HIGH);
  delayConWdt(cfgCleanPulseMs);
  digitalWrite(PIN_RELAY_HABILITA, LOW);
  logLine(">> Limpieza: fin segundo pulso, esperando 20s...");
  delayConWdt(20000);

  logLine(">> Ciclo de limpieza terminado.");
  usedCredits = 0;
  guardarUsedCredits();
}

// ============================================================================
//            CALENTADOR (TODO: confirmar PIN_CALENTADOR fisicamente)
// ============================================================================
void actualizarCalentador() {
  unsigned long ahora = millis();
  unsigned long intervaloMs = (heaterOn ? (unsigned long)cfgHeatOnMin : (unsigned long)cfgHeatOffMin) * 60000UL;

  if (ahora - heaterLastToggleMs >= intervaloMs) {
    heaterOn = !heaterOn;
    digitalWrite(PIN_CALENTADOR, heaterOn ? HIGH : LOW);
    heaterLastToggleMs = ahora;
    logLine(heaterOn ? ">> Calentador ACTIVADO." : ">> Calentador DESACTIVADO.");
  }
}

// ============================================================================
//            MERCADO PAGO — polling directo desde la placa (a propósito,
//            no se mueve a un webhook del servidor — ver comentario arriba)
// ============================================================================
bool consultarMercadoPago() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (cfgMpAccessToken == "" || cfgMpCollectorId == "" || cfgMpExternalRef == "") {
    logLine("!! MP: faltan datos de config (token/collector/external_reference). Sin consultar.");
    return false;
  }

  String url = "https://api.mercadopago.com/v1/payments/search"
               "?limit=1&offset=0&operation_type=regular_payment"
               "&status=approved&sort=date_last_updated&criteria=desc"
               "&external_reference=" + cfgMpExternalRef;

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(6000);
  if (!http.begin(url)) {
    logLine("!! MP: http.begin() fallo.");
    return false;
  }
  http.addHeader("Authorization", "Bearer " + cfgMpAccessToken);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");

  int httpStatus = http.GET();
  if (httpStatus != 200) {
    logLine("!! MP: respuesta no OK (status " + String(httpStatus) + ").");
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();
  if (body.length() == 0) return false;

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    logLine("!! MP: no se pudo parsear el JSON de respuesta.");
    return false;
  }

  JsonArray results = doc["results"].as<JsonArray>();
  if (results.size() == 0) return false;

  JsonObject pago = results[0];
  long collectorRecibido = pago["collector_id"] | 0L;
  String statusRecibido = pago["status"] | "";
  String idPago = String((long)(pago["id"] | 0L));

  if (String(collectorRecibido) != cfgMpCollectorId) return false;
  if (statusRecibido != "approved") return false;
  if (idPago == "" || idPago == lastPaymentId) return false;

  logLine(">> MP: nuevo credito detectado. ID: " + idPago);
  lastPaymentId = idPago;
  guardarLastPaymentId(lastPaymentId);
  usedCredits++;
  guardarUsedCredits();
  return true;
}

// ============================================================================
//                    CHECK-IN CONTRA NUESTRO BACKEND (socket TLS crudo)
// ============================================================================
// Copiado tal cual del esqueleto de la garra — nunca HTTPClient a propósito,
// timeouts duros en cada paso para que esta función siempre vuelva en bien
// menos de los 30s del watchdog, pase lo que pase del otro lado.
bool backendCheckin(JsonDocument &respuesta) {
  const unsigned long CONNECT_TIMEOUT_MS = 5000;
  const unsigned long RESPUESTA_DEADLINE_MS = 6000;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(CONNECT_TIMEOUT_MS / 1000);

  if (!client.connect(BACKEND_HOST, BACKEND_PORT, CONNECT_TIMEOUT_MS)) {
    logLine("!! checkin: no se pudo conectar al backend.");
    return false;
  }

  String mac = WiFi.macAddress();

  StaticJsonDocument<256> bodyDoc;
  bodyDoc["mac"] = mac;
  bodyDoc["factory_key"] = FACTORY_KEY;
  bodyDoc["firmware_version"] = FIRMWARE_VERSION;
  bodyDoc["wifi_rssi"] = WiFi.RSSI();
  // El backend sólo usa esto para decidir el "kind" en el alta de una
  // placa NUEVA (ver api/device/checkin/route.ts) — en una placa ya
  // existente no cambia nada, así que mandarlo siempre es inofensivo.
  bodyDoc["kind"] = "cafetera";
  String body;
  serializeJson(bodyDoc, body);

  String request = "POST " + String(CHECKIN_PATH) + " HTTP/1.1\r\n";
  request += "Host: " + String(BACKEND_HOST) + "\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Content-Length: " + String(body.length()) + "\r\n";
  request += "Connection: close\r\n\r\n";
  request += body;

  client.print(request);

  unsigned long tEsperaInicio = millis();
  while (client.connected() && !client.available()) {
    if (millis() - tEsperaInicio > RESPUESTA_DEADLINE_MS) {
      logLine("!! checkin: timeout esperando respuesta.");
      client.stop();
      return false;
    }
    delay(10);
    esp_task_wdt_reset();
  }

  String statusLine = client.available() ? client.readStringUntil('\n') : "";
  statusLine.trim();

  bool chunked = false;
  unsigned long tHeaders0 = millis();
  while (client.connected() || client.available()) {
    if (millis() - tHeaders0 > RESPUESTA_DEADLINE_MS) {
      logLine("!! checkin: timeout leyendo headers.");
      client.stop();
      return false;
    }
    if (!client.available()) { delay(5); continue; }
    String linea = client.readStringUntil('\n');
    linea.trim();
    if (linea.indexOf("Transfer-Encoding: chunked") >= 0) chunked = true;
    if (linea.length() == 0) break;
  }

  String payload = "";
  payload.reserve(2560); // cafe_config + commands + firmware_update puede pesar varios cientos de bytes
  unsigned long tLectura0 = millis();
  while ((client.connected() || client.available()) &&
         millis() - tLectura0 < RESPUESTA_DEADLINE_MS) {
    while (client.available()) {
      payload += (char)client.read();
    }
    if (!client.connected() && !client.available()) break;
  }
  client.stop();

  if (chunked) {
    int inicio = payload.indexOf('{');
    int fin = payload.lastIndexOf('}');
    if (inicio >= 0 && fin > inicio) {
      payload = payload.substring(inicio, fin + 1);
    }
  }

  if (DEBUG_LOG) {
    logLine("---- checkin " + statusLine);
    logLine(payload);
  }

  if (payload.length() == 0) {
    logLine("!! checkin: respuesta vacia.");
    return false;
  }

  DeserializationError err = deserializeJson(respuesta, payload);
  if (err) {
    logLine("!! checkin: no se pudo parsear el JSON de respuesta.");
    return false;
  }
  return true;
}

void procesarComandos(JsonArray comandos) {
  for (JsonVariant c : comandos) {
    String comando = c.as<String>();
    logLine(">> Comando recibido: " + comando);

    if (comando == "restart") {
      logLine(">> Reiniciando por orden del panel...");
      delay(200);
      ESP.restart();
    } else if (comando == "test_dispense") {
      // Pulso corto DIRECTO sobre el relé, sin esperar el pulso real de fin
      // de ciclo — es sólo para probar cableado/relé desde el panel, no un
      // servido real de café.
      logLine(">> test_dispense: pulso de prueba de 500ms en GPIO25.");
      digitalWrite(PIN_RELAY_HABILITA, HIGH);
      delay(500);
      digitalWrite(PIN_RELAY_HABILITA, LOW);
    } else if (comando == "clean_cycle") {
      // Limpieza manual pedida desde la app ("🧽 Limpiar ahora"). Si hay un
      // ciclo de servido en curso lo dejamos terminar antes de arrancar.
      if (relayActivo) {
        logLine("!! clean_cycle: hay un ciclo de servido en curso, se ignora por ahora.");
      } else {
        ejecutarCicloDeLimpieza();
      }
    } else {
      logLine("!! Comando desconocido, se ignora: " + comando);
    }
  }
}

// ============================================================================
//              ACTUALIZACIÓN REMOTA DE FIRMWARE (OTA) — igual que la garra
// ============================================================================
void ejecutarActualizacionFirmware(const String &version, const String &binUrl) {
  if (actualizacionFirmwareEnCurso) return;
  actualizacionFirmwareEnCurso = true;

  logLine(">> Firmware: actualizacion disponible (" + version + "). Descargando...");

  esp_task_wdt_delete(NULL);

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(false);

  t_httpUpdate_return resultado = httpUpdate.update(client, binUrl);

  esp_task_wdt_add(NULL);

  switch (resultado) {
    case HTTP_UPDATE_FAILED:
      logLine("!! Firmware: fallo la actualizacion (" + String(httpUpdate.getLastErrorString().c_str()) + ").");
      actualizacionFirmwareEnCurso = false;
      break;
    case HTTP_UPDATE_NO_UPDATES:
      logLine("!! Firmware: el servidor no devolvio un .bin valido.");
      actualizacionFirmwareEnCurso = false;
      break;
    case HTTP_UPDATE_OK:
      logLine(">> Firmware: actualizacion OK. Reiniciando con " + version + "...");
      delay(300);
      ESP.restart();
      break;
  }
}

void hacerCheckin() {
  StaticJsonDocument<3072> doc;
  bool ok = backendCheckin(doc);

  if (!ok) {
    checkinFailCount++;
    return;
  }
  checkinOkCount++;

  String status = doc["status"] | "";
  deviceStatus = status;

  if (status == "revoked") return;

  if (status == "unclaimed") {
    claimCode = String((const char *)(doc["claim_code"] | ""));
    if (claimCode.length() > 0) {
      logLine(">> Placa sin reclamar. Codigo de alta: " + claimCode);
    }
    return;
  }

  if (status == "claimed") {
    devicePaused = doc["is_paused"] | false;

    if (doc["cafe_config"].is<JsonObject>()) {
      aplicarCafeConfig(doc["cafe_config"].as<JsonObject>());
    }

    if (doc["commands"].is<JsonArray>()) {
      procesarComandos(doc["commands"].as<JsonArray>());
    }

    if (doc["firmware_update"].is<JsonObject>()) {
      String nuevaVersion = doc["firmware_update"]["version"] | "";
      String binUrl = doc["firmware_update"]["bin_url"] | "";
      if (nuevaVersion.length() > 0 && binUrl.length() > 0) {
        ejecutarActualizacionFirmware(nuevaVersion, binUrl);
      }
    }
  }
}

// ============================================================================
//                    SALUD DEL SISTEMA (heap / uptime / wifi)
// ============================================================================
void chequearSaludYReiniciarSiHaceFalta() {
  // Nunca en medio de un ciclo de servido o de limpieza.
  if (relayActivo) return;

  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < HEAP_SAFETY_THRESHOLD) {
    logLine("!! Heap bajo (" + String(freeHeap) + " bytes). Reinicio preventivo.");
    delay(200);
    ESP.restart();
  }

  if (millis() - bootAt > RESTART_INTERVAL_MS) {
    logLine(">> Reinicio preventivo por uptime.");
    delay(200);
    ESP.restart();
  }

  if (WiFi.status() == WL_CONNECTED) {
    lastWifiOkAt = millis();
  } else if (millis() - lastWifiOkAt > WIFI_DOWN_RESTART_MS) {
    logLine("!! WiFi caido hace demasiado tiempo. Reinicio completo.");
    delay(200);
    ESP.restart();
  }
}

void configurarWatchdog(uint32_t timeoutS) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = timeoutS * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&twdt_config);
#else
  esp_task_wdt_init(timeoutS, true);
#endif
}

// ============================================================================
//            PORTAL DE CONFIGURACION (bajo demanda, SOLO WiFi)
// ============================================================================
// A diferencia del firmware MPAY original, este portal NO tiene ningun
// WiFiManagerParameter custom — ni ID comercio, ni token, ni tiempos. Todo
// eso ahora es "desde la app". Acá sólo se elige la red WiFi.
void abrirPortalDeConfiguracion() {
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(100);

  configurarWatchdog(WDT_TIMEOUT_PORTAL_S);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0);
  wm.startConfigPortal("QRcade_Config");
  configurarWatchdog(WDT_TIMEOUT_S);

  const unsigned long TIMEOUT_SIN_CONFIGURAR_MS = 180000; // 3 min
  unsigned long inicioPortal = millis();

  while (WiFi.status() != WL_CONNECTED) {
    esp_task_wdt_reset();
    wm.process();

    if (millis() - inicioPortal >= TIMEOUT_SIN_CONFIGURAR_MS) {
      logLine("!! Portal de configuracion: timeout sin que nadie configure. Reiniciando...");
      wm.stopConfigPortal();
      delay(300);
      ESP.restart();
    }
    delay(10);
  }

  logLine(">> Portal de configuracion: WiFi configurado con exito.");
  guardarCredencialesWifi(wm.getWiFiSSID(), wm.getWiFiPass());
}

bool botonConfigMantenidoAlBoot() {
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(CONFIG_BUTTON_PIN) != LOW) return false;

  unsigned long t0 = millis();
  while (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    if (millis() - t0 >= CONFIG_BUTTON_HOLD_MS) return true;
    delay(20);
  }
  return false;
}

// Chequeo del boton BOOT EN OPERACION NORMAL (no al arrancar) — mantenido
// FACTORY_RESET_HOLD_MS borra todo (WiFi + config cacheada) y reinicia.
// Mismo mecanismo "de emergencia" que tenia el firmware original.
void chequearBotonFactoryResetEnLoop() {
  if (digitalRead(CONFIG_BUTTON_PIN) != LOW) return;

  unsigned long t0 = millis();
  while (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    esp_task_wdt_reset();
    if (millis() - t0 >= FACTORY_RESET_HOLD_MS) {
      logLine(">> BOOT mantenido en operacion -> borrado de fabrica.");
      borrarWifiYConfigCompleta();
    }
    delay(20);
  }
}

// ============================================================================
//                              SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_err_t wdtInitErr = esp_task_wdt_init(&twdt_config);
  if (wdtInitErr == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&twdt_config);
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);

  // El relé SIEMPRE arranca inactivo (LOW), sin excepciones.
  pinMode(PIN_RELAY_HABILITA, OUTPUT);
  digitalWrite(PIN_RELAY_HABILITA, LOW);

  pinMode(PIN_CALENTADOR, OUTPUT); // TODO: confirmar pin real
  digitalWrite(PIN_CALENTADOR, LOW);

  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  delay(100);

  logLine("");
  logLine("========================================");
  logLine("QRcade CAFETERA firmware v" + String(FIRMWARE_VERSION));
  logLine("MAC: " + WiFi.macAddress());
  logLine("========================================");

  // Config operativa cacheada primero (incluye el pinMode correcto del
  // sensor de fin de ciclo según el nivel configurado) — así, aunque el
  // primer check-in tarde o falle, la placa ya puede operar con lo último
  // conocido.
  cargarCafeConfigCacheada();

  bool pedirPortal = botonConfigMantenidoAlBoot();
  if (pedirPortal) logLine(">> Boton de configuracion detectado al arrancar.");

  esp_task_wdt_reset();

  String ssidGuardado, passGuardado;
  bool hayCredenciales = leerCredencialesWifi(ssidGuardado, passGuardado);
  if (hayCredenciales) {
    logLine(">> Credenciales WiFi guardadas encontradas para SSID: " + ssidGuardado);
  } else {
    logLine(">> No hay credenciales WiFi guardadas.");
  }

  if (pedirPortal || !hayCredenciales) {
    abrirPortalDeConfiguracion();
  } else {
    WiFi.begin(ssidGuardado.c_str(), passGuardado.c_str());
    unsigned long tConexion0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - tConexion0 < 20000) {
      delay(250);
      esp_task_wdt_reset();
    }
    if (WiFi.status() != WL_CONNECTED) {
      logLine("!! No conecto con credenciales guardadas. Abriendo portal...");
      abrirPortalDeConfiguracion();
    }
  }

  logLine(">> Conectado a WiFi. IP: " + WiFi.localIP().toString());

  bootAt = millis();
  lastWifiOkAt = millis();

  char nombreDispositivo[20];
  snprintf(nombreDispositivo, sizeof(nombreDispositivo), "QRCAFE-%06lu",
           (unsigned long)(ESP.getEfuseMac() & 0xFFFFFF));
  iniciarBLE(String(nombreDispositivo));

  hacerCheckin();

  esp_task_wdt_reset();
  logLine(">> Setup completo. Arrancando loop().");
}

// ============================================================================
//                               LOOP
// ============================================================================
void loop() {
  esp_task_wdt_reset();

  chequearBotonFactoryResetEnLoop();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectTry = 0;
    if (millis() - lastReconnectTry > 10000) {
      lastReconnectTry = millis();
      logLine("!! WiFi desconectado, reintentando...");
      WiFi.reconnect();
    }
  } else {
    if (millis() - lastCheckinAt >= CHECKIN_INTERVAL_MS) {
      lastCheckinAt = millis();
      hacerCheckin();
    }
  }

  // Calentador: corre siempre que haya config cargada, sin importar el
  // estado de reclamo/pausa — es la temperatura del agua, no un cobro.
  if (cafeConfigCargada) {
    actualizarCalentador();
  }

  // El ciclo de servido/pago sólo corre si la placa está reclamada, no
  // pausada, y ya tenemos alguna config (cacheada o fresca) para operar.
  bool puedeOperar = (deviceStatus == "claimed") && !devicePaused && cafeConfigCargada;

  if (relayActivo && esperandoFinDeCiclo) {
    // Un ciclo en curso se deja terminar SIEMPRE, incluso si justo en el
    // medio la placa se pausó o se revocó desde el panel — cortar el relé
    // a mitad de un servido real sería peor.
    if (sensarPulsoFinDeCiclo(30000UL)) {
      esperandoFinDeCiclo = false;
      desactivarRelayConRetardo();

      if (usedCredits >= (uint32_t)cfgCleanEveryNCycles) {
        ejecutarCicloDeLimpieza();
      }
    }
  } else if (puedeOperar) {
    unsigned long ahora = millis();
    if (ahora - lastMpCheckMs >= MP_CHECK_INTERVAL_MS) {
      lastMpCheckMs = ahora;
      if (consultarMercadoPago()) {
        activarRelay();
      }
    }
  }

  chequearSaludYReiniciarSiHaceFalta();

  if (Serial.available() > 0) {
    char key = Serial.read();
    if (key == 'c' || key == 'C') {
      digitalWrite(PIN_RELAY_HABILITA, HIGH);
      delay(500);
      digitalWrite(PIN_RELAY_HABILITA, LOW);
    } else if (key == 'h' || key == 'H') {
      Serial.println("Heap libre: " + String(ESP.getFreeHeap()) + " bytes");
      Serial.println("Check-ins OK: " + String(checkinOkCount) + "  fallidos: " + String(checkinFailCount));
      Serial.println("Uptime: " + String((millis() - bootAt) / 1000) + "s");
      Serial.println("Creditos usados desde ultima limpieza: " + String(usedCredits) + "/" + String(cfgCleanEveryNCycles));
      Serial.println("MP external_reference: " + cfgMpExternalRef);
    }
  }

  delay(10);
}
