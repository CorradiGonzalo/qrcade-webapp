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
       calentador, limpieza automática y manual, y el polling directo a la
       API de Mercado Pago desde la ESP32 (a pedido explícito del dueño,
       esto se queda en la placa — no se mueve a un webhook del servidor).

  SE SACÓ EL BLE POR COMPLETO (reset remoto por Bluetooth, que tenía el
  programa original): la placa se maneja entera desde la app — el reinicio
  remoto sigue existiendo, pero sólo vía comando "restart" desde el panel.
  De paso, sacar BLE le devuelve a la placa memoria/radio que esa pila
  reservaba de forma permanente (candidato fuerte a por qué "maxAlloc"
  quedaba siempre pisado en ~34KB sin importar el heap total libre) y
  elimina la contención WiFi/Bluetooth por la misma antena — una causa
  real de las fallas de conexión intermitentes que venía mostrando esta
  placa tanto contra el backend como contra Mercado Pago.

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
    - Preferences / HTTPClient / HTTPUpdate
      (todas incluidas en el core ESP32, no hace falta instalar nada aparte)

  Partition Scheme en Arduino IDE: sin BLE el sketch pesa bastante menos
  que antes, así que "Default 4MB with spiffs" debería entrar sin drama.
  Si por algún motivo no entra, usar "Minimal SPIFFS (1.9MB APP with OTA)"
  como antes — no hace daño, sólo da más margen.
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
//                          BLINDAJE / HARDENING (igual que la garra)
// ============================================================================
static const bool DEBUG_LOG = true;

static const uint32_t WDT_TIMEOUT_S = 30;
static const uint32_t WDT_TIMEOUT_PORTAL_S = 90;

static const unsigned long WIFI_DOWN_RESTART_MS = 5UL * 60UL * 1000UL;        // 5 min
static const unsigned long RESTART_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL; // 12 h
static const uint32_t HEAP_SAFETY_THRESHOLD = 20000;                          // bytes

// IMPORTANTE: el check-in contra nuestro backend DEJÓ de ser "sólo
// config/pausa/comandos no urgentes". Resulta que el comando
// "dispense:N" que manda el webhook de Mercado Pago cuando confirma un
// pago (ver procesarComandos) es, en la práctica, la detección de pago
// que SÍ funciona — la consulta directa a MP desde la placa
// (consultarMercadoPago) viene devolviendo 200 OK sin encontrar el pago
// bajo la Orders API nueva. Mientras no se resuelva eso del lado de MP,
// el check-in es el camino real por el que se entera un pago, así que
// tiene que ser rápido — lo mismo que la consulta a MP. Habíamos subido
// este intervalo a 2 minutos para aliviar la antena mientras sospechábamos
// de contención entre las dos conexiones TLS, pero esa inestabilidad
// resultó ser el BLE (ya sacado, ver cabecera del archivo) y no la
// frecuencia: con BLE afuera, heap y maxAlloc quedaron sanos y las
// conexiones salen bien una tras otra, así que lo volvemos a bajar.
static const unsigned long CHECKIN_INTERVAL_MS = 10000;

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
bool cfgPulsoNivelHigh = true;

bool cafeConfigCargada = false; // true en cuanto hay algo (cacheado o fresco) para operar

static const char *CAFE_PREFS_NS = "cafecfg";

// ============================================================================
//            ESTADO DE PAGOS / CICLO DE SERVIDO / LIMPIEZA
// ============================================================================
String lastPaymentId = "";
uint32_t usedCredits = 0;
unsigned long lastMpCheckMs = 0;
// El firmware original consultaba 1 vez por minuto ("Esperando 1 minuto sin
// verificar pagos..."). Eso significa que un cliente que paga puede llegar
// a esperar casi 60s a que se habilite la botonera — inaceptable para una
// máquina real. Lo bajamos a 10s: sigue siendo una consulta liviana (no es
// nada pesado para la API de MP), y un cliente esperando como mucho 10s es
// razonable. Si en el futuro hay MUCHAS cafeteras compartiendo el mismo
// Access Token de MP y aparecen límites de rate, este es el primer lugar
// para subir el valor de nuevo (o volverlo configurable desde el panel,
// como el resto de cafe_config).
static const unsigned long MP_CHECK_INTERVAL_MS = 10000;

// El check-in (cada 15s) y la consulta a MP (cada 10s) son independientes,
// así que tarde o temprano sus timers caen en la MISMA vuelta de loop() —
// ahí se abren dos conexiones TLS seguidas, una justo después de la otra,
// sin que la pila WiFi/lwIP tenga margen para terminar de liberar la
// anterior. En los logs reales eso coincide con rachas donde check-in Y
// MP fallan juntos (ninguno de los dos por falta de heap: maxAlloc se
// mantiene estable). Este timestamp compartido obliga a dejar un
// colchón mínimo entre cualquier par de conexiones TLS, vengan de
// check-in o de MP, para darle respiro a la pila de red entre una y otra.
unsigned long lastRedOpMs = 0;
static const unsigned long MIN_GAP_ENTRE_OPS_RED_MS = 2000;

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
  if (!cfg["pulso_nivel_high"].isNull()) cfgPulsoNivelHigh = cfg["pulso_nivel_high"].as<bool>();

  configurarPinPulso();
  guardarCafeConfigCacheada();
  cafeConfigCargada = true;
}

// ============================================================================
//            BORRADO DE FABRICA (botón BOOT en operación, o comando remoto)
// ============================================================================
// Antes esto también se podía disparar por BLE (reset remoto por Bluetooth,
// igual que el programa original). Se sacó todo el BLE a pedido explícito:
// la idea es que la placa se maneje entera desde la app — y de paso, sacar
// BLE libera la memoria/radio que esa pila reservaba de forma permanente
// (es la causa más probable de que maxAlloc quedara siempre pisado en
// ~34KB, sin importar cuánto heap total hubiera libre) y elimina la
// contención WiFi/BT por la misma antena. El reinicio remoto sigue
// existiendo igual — ahora SOLO por el comando "restart" que manda la app
// vía check-in (ver procesarComandos más abajo).
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

  const unsigned long CONNECT_TIMEOUT_MS = 5000;
  const unsigned long RESPUESTA_DEADLINE_MS = 6000;
  const char* MP_HOST = "api.mercadopago.com";
  const int MP_PORT = 443;

  String path = "/v1/payments/search"
                "?limit=1&offset=0&operation_type=regular_payment"
                "&status=approved&sort=date_last_updated&criteria=desc"
                "&external_reference=" + cfgMpExternalRef;

  logLine(">> MP: heap libre=" + String(ESP.getFreeHeap()) +
          " maxAlloc=" + String(ESP.getMaxAllocHeap()) + " antes de consultar.");

  // Antes usábamos HTTPClient (como en el check-in, primer intento), pero
  // seguía fallando con "status -1" incluso pasándole un WiFiClientSecure
  // propio con setInsecure(). El log de heap mostró que maxAlloc quedaba
  // siempre pisado en ~34KB sin importar cuánto heap total hubiera libre
  // — fragmentación, no falta de memoria. Esto coincidía con que BLE
  // estaba corriendo (esa pila reserva un bloque de memoria fijo que ya
  // no se puede recuperar mientras esté activa); sacar BLE por completo
  // (ver cabecera del archivo) debería haber liberado esa parte. Igual
  // se deja esta consulta SIN HTTPClient: abre el socket TLS a mano y
  // arma/parsea el HTTP como en backendCheckin(), que es el mismo patrón
  // ya probado en producción con las garras y pesa menos memoria.
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(CONNECT_TIMEOUT_MS / 1000);

  if (!client.connect(MP_HOST, MP_PORT, CONNECT_TIMEOUT_MS)) {
    // RSSI y heap en el momento exacto del fallo — si esto pasa en racha
    // junto con fallos de check-in, el patrón es contención entre las dos
    // conexiones TLS seguidas, no falta de señal ni de memoria.
    logLine("!! MP: no se pudo conectar (status -1). RSSI=" + String(WiFi.RSSI()) +
            " heapLibre=" + String(ESP.getFreeHeap()));
    return false;
  }

  String request = "GET " + path + " HTTP/1.1\r\n";
  request += "Host: " + String(MP_HOST) + "\r\n";
  request += "Authorization: Bearer " + cfgMpAccessToken + "\r\n";
  request += "Accept: application/json\r\n";
  request += "Accept-Encoding: identity\r\n";
  request += "Connection: close\r\n\r\n";

  client.print(request);

  unsigned long tEsperaInicio = millis();
  while (client.connected() && !client.available()) {
    if (millis() - tEsperaInicio > RESPUESTA_DEADLINE_MS) {
      logLine("!! MP: timeout esperando respuesta.");
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
      logLine("!! MP: timeout leyendo headers.");
      client.stop();
      return false;
    }
    if (!client.available()) { delay(5); continue; }
    String linea = client.readStringUntil('\n');
    linea.trim();
    if (linea.indexOf("Transfer-Encoding: chunked") >= 0) chunked = true;
    if (linea.length() == 0) break;
  }

  String body = "";
  body.reserve(2560);
  unsigned long tLectura0 = millis();
  while ((client.connected() || client.available()) &&
         millis() - tLectura0 < RESPUESTA_DEADLINE_MS) {
    while (client.available()) {
      body += (char)client.read();
    }
    if (!client.connected() && !client.available()) break;
  }
  client.stop();

  if (chunked) {
    int inicio = body.indexOf('{');
    int fin = body.lastIndexOf('}');
    if (inicio >= 0 && fin > inicio) {
      body = body.substring(inicio, fin + 1);
    }
  }

  if (DEBUG_LOG) {
    logLine("---- MP " + statusLine);
  }

  // El código de estado HTTP real viene en la statusLine ("HTTP/1.1 200 OK...").
  // Si no es 200, no tiene sentido intentar parsear el body.
  if (statusLine.indexOf(" 200 ") < 0) {
    logLine("!! MP: respuesta no OK (" + statusLine + ").");
    return false;
  }

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
  // Los ID de pago de MP pueden tener 11-13 dígitos y no entran en un
  // "long" de 32 bits (máx ~2.1 mil millones) — convertirlos a (long) los
  // truncaba, lo que podía romper la comparación de deduplicado contra
  // lastPaymentId. ArduinoJson preserva el valor grande al parsear; sólo
  // había que evitar el cast y pedirlo directamente como String.
  String idPago = pago["id"].as<String>();

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
    logLine("!! checkin: no se pudo conectar al backend. RSSI=" + String(WiFi.RSSI()) +
            " heapLibre=" + String(ESP.getFreeHeap()));
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
      // "Café gratis" de prueba: arranca EXACTAMENTE el mismo ciclo que
      // dispara un pago real detectado por Mercado Pago (activarRelay()
      // deja la botonera habilitada y el loop() espera el pulso real de
      // fin de ciclo en GPIO27, igual que con un pago de verdad) — sólo
      // que sin que nadie haya pagado. Así se prueba la habilitación de
      // la botonera de punta a punta, no sólo que el relé hace clic.
      if (relayActivo) {
        logLine("!! test_dispense: ya hay un ciclo en curso, se ignora.");
      } else {
        logLine(">> test_dispense: arrancando ciclo de prueba (cafe gratis).");
        activarRelay();
      }
    } else if (comando == "clean_cycle") {
      // Limpieza manual pedida desde la app ("🧽 Limpiar ahora"). Si hay un
      // ciclo de servido en curso lo dejamos terminar antes de arrancar.
      if (relayActivo) {
        logLine("!! clean_cycle: hay un ciclo de servido en curso, se ignora por ahora.");
      } else {
        ejecutarCicloDeLimpieza();
      }
    } else if (comando.startsWith("dispense:")) {
      // Esto lo manda el webhook de Mercado Pago (api/mp/webhook/route.ts)
      // cuando confirma el pago de la orden — es la MISMA detección que ya
      // usa la garra, y es la que de verdad está probada en producción.
      // El polling directo a MP desde esta placa (consultarMercadoPago())
      // se mantiene por la latencia (hasta 10s contra hasta 2min de acá),
      // pero en la práctica viene devolviendo 200 OK sin encontrar el pago
      // — probablemente porque la Orders API nueva de MP no deja el mismo
      // external_reference en el Payment que generó, sólo en la Order. En
      // vez de depender de eso, usamos este comando como red de seguridad
      // real: si llega, significa que el webhook SÍ encontró el pago.
      if (relayActivo) {
        logLine("!! dispense: ya hay un ciclo en curso, se ignora: " + comando);
      } else {
        logLine(">> dispense: pago confirmado por el panel, habilitando botonera.");
        usedCredits++;
        guardarUsedCredits();
        activarRelay();
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

    // Importante: re-chequear la conexión ACÁ, inmediatamente después de
    // wm.process() y ANTES de evaluar el timeout. Si alguien terminó de
    // configurar la red justo sobre el límite de los 3 minutos, wm.process()
    // ya la deja conectada en esta misma vuelta — sin este chequeo, el
    // timeout se disparaba igual (ESP.restart() sobre un WiFiManager que
    // acababa de conectar), lo que provocaba un crash
    // (Guru Meditation Error: LoadProhibited) y reinicio inesperado justo
    // después de "Connect to new AP [SUCCESS]".
    if (WiFi.status() == WL_CONNECTED) break;

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
    if (millis() - lastCheckinAt >= CHECKIN_INTERVAL_MS &&
        millis() - lastRedOpMs >= MIN_GAP_ENTRE_OPS_RED_MS) {
      lastCheckinAt = millis();
      lastRedOpMs = lastCheckinAt;
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
    if (ahora - lastMpCheckMs >= MP_CHECK_INTERVAL_MS &&
        ahora - lastRedOpMs >= MIN_GAP_ENTRE_OPS_RED_MS) {
      lastMpCheckMs = ahora;
      lastRedOpMs = ahora;
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
