/*
  ============================================================================
  QRcade — Firmware v2.0 (blindado contra cuelgues)
  ============================================================================
  Escrito de cero. Habla ÚNICAMENTE con nuestro backend propio (el panel
  qrcade.dev) — nunca con Mercado Pago directo. Toda la lógica de pagos,
  QR de cobro y aprovisionamiento vive en el servidor; esta placa sólo:

    1. Se conecta a WiFi.
    2. Hace check-in periódico a POST /api/device/checkin.
    3. Dibuja en pantalla lo que el backend le dice (QR, estados, etc).
    4. Ejecuta los comandos que el backend le manda (pulso de ficha, reinicio).


  Pines (tomados del firmware anterior con pantalla — no cambian):
    - Relé:            GPIO 25
    - Pantalla ST7789:  SCK=18  MOSI=23  MISO=(sin usar)  RST=4  DC=19  CS=(sin usar)
    - Resolución:       240x240

  ----------------------------------------------------------------------------
  NOVEDAD v2.0.2: modo "combo" (monto abierto) con menú en pantalla. Antes,
  el modo combo abría el QR a cualquier monto pero la placa seguía
  dibujando el QR de siempre a pantalla completa — el cliente no tenía
  forma de saber qué monto pagar para qué cantidad de fichas. Ahora, si el
  backend manda mode:"combo" junto con una lista de combos (ver
  /api/device/checkin), se dibuja el QR más chico arriba y la lista
  "$monto -> N ficha(s)" debajo. Si todavía no hay ningún combo cargado
  desde el panel, se cae al QR de siempre a pantalla completa (mejor
  mostrar algo que nada, aunque sin combos configurados ningún pago va a
  acreditar fichas).

  ----------------------------------------------------------------------------
  QUÉ SE BLINDÓ RESPECTO A LOS FIRMWARES ANTERIORES (y por qué):

  1. CERO llamadas directas a APIs externas de pago. El firmware viejo abría
     una conexión TLS nueva cada 1-5s contra la API de Mercado Pago desde la
     placa, con JSON pesados. Eso fragmentaba el heap hora tras hora hasta
     que un malloc fallaba y la placa quedaba tildada. Acá el único destino
     de red es nuestro propio backend, con un payload chico y bien acotado.

  2. Portal de configuración WiFi NUNCA se abre solo. Los firmwares
     anteriores prendían el Access Point (modo STA+AP simultáneo) por 30s en
     cada arranque, en paralelo con el polling de pagos y el dibujo de
     pantalla — ESP32 en modo dual-radio bajo carga de TLS es un combo
     conocido por colgarse. Acá el portal SOLO se abre si:
       a) no hay credenciales WiFi guardadas, o
       b) el usuario mantiene apretado el botón BOOT (GPIO 0, el mismo que
          se usa para flashear — no hace falta hardware extra) durante el
          arranque.
     En operación normal jamás hay dos radios activos a la vez.

  3. Watchdog de hardware (Task Watchdog Timer). Si el loop() se cuelga más
     de WDT_TIMEOUT_S segundos por lo que sea (una librería, un cuelgue de
     red no contemplado, lo que sea), el propio chip se reinicia solo. Antes
     no había nada que recuperara la placa sin desenchufarla a mano.

  4. Check-in por socket TLS crudo con timeouts duros en cada paso (conectar,
     esperar respuesta, leer body) — nunca puede quedar esperando para
     siempre como sí puede pasar con HTTPClient en algunos casos límite.

  5. Reconexión de WiFi activa. Si se cae el WiFi, se reintenta solo con
     backoff; si pasan WIFI_DOWN_RESTART_MS sin reconectar, se reinicia la
     placa entera (a veces el stack de WiFi del ESP32 queda en un estado
     raro que sólo un reinicio limpia).

  6. Auto-reinicio preventivo cada RESTART_INTERVAL_MS de uptime continuo.
     Es un parche "de fuerza bruta" pero muy efectivo: limpia cualquier
     fragmentación de heap que se haya ido acumulando antes de que llegue a
     ser un problema. Se hace de forma segura (nunca a mitad de un pulso de
     ficha).

  7. Chequeo de heap libre en cada check-in. Si en algún momento la memoria
     libre cae debajo de HEAP_SAFETY_THRESHOLD, se reinicia preventivamente
     en vez de esperar a que explote un malloc en el peor momento posible
     (a mitad de un cobro).

  8. Relé siempre arranca y queda en HIGH (inactivo) ante cualquier reinicio,
     boot o crash — nunca puede quedar activado por accidente.

  Librerías necesarias (Arduino Library Manager):
    - WiFiManager (tzapu)
    - ArduinoJson
    - GFX Library for Arduino (moononournation / Arduino_GFX_Library)
    - QRCode (ricmoo) — se usa la copia local qrcode_impl.c/h, no hace falta
      instalarla aparte.
  ============================================================================
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <esp_task_wdt.h>
#include <HTTPUpdate.h>
#include "qrcode_impl.h"

// ============================================================================
//                          CONFIG DE FÁBRICA (por tanda)
// ============================================================================
// Misma clave para toda la tanda de placas que arranca con este .ino. El hash
// correspondiente vive en app_settings.factory_key_hash en Supabase. Generada
// con `openssl rand -hex 24`.
static const char *FACTORY_KEY = "6d250dc5d880fe75c824a4c9eb64520cca1d270961ae17f9";

// Host del backend (panel QRcade), sin "https://" ni barra final.
static const char *BACKEND_HOST = "qrcade.dev";
static const uint16_t BACKEND_PORT = 443;
static const char *CHECKIN_PATH = "/api/device/checkin";

static const char *FIRMWARE_VERSION = "2.0.4";

// ============================================================================
//                          CONFIG DE HARDWARE
// ============================================================================
static const int RELAY_PIN = 25;
static const unsigned long PULSE_DURATION_MS = 500;
static const int RELAY_INACTIVE = HIGH;
static const int RELAY_ACTIVE = LOW;

#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_MISO  -1
#define TFT_RST   4
#define TFT_DC    19
#define TFT_CS    -1

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

// Botón BOOT de la placa (GPIO 0), el mismo que se usa para flashear por
// USB. Se lee a nivel bajo = apretado. No requiere cableado extra.
static const int CONFIG_BUTTON_PIN = 0;
static const unsigned long CONFIG_BUTTON_HOLD_MS = 2000;

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0, true, SCREEN_WIDTH, SCREEN_HEIGHT);

#define COLOR_NEGRO   0x0000
#define COLOR_BLANCO  0xFFFF
#define COLOR_VERDE   0x07E0
#define COLOR_ROJO    0xF800
#define COLOR_AZUL    0x051D
#define COLOR_GRIS    0x7BEF
#define COLOR_AMARILLO 0xFFE0

// ============================================================================
//                          BLINDAJE / HARDENING
// ============================================================================
static const bool DEBUG_LOG = true;

// Watchdog: si el loop no "respira" en este tiempo, el chip se reinicia solo.
static const uint32_t WDT_TIMEOUT_S = 30;

// Timeout de watchdog "ampliado", usado sólo mientras dura
// wm.startConfigPortal() (ver abrirPortalDeConfiguracion()) — ese llamado
// hace un WiFi.scanNetworks() sincrónico que de por sí puede tardar más que
// WDT_TIMEOUT_S normal. Antes sacábamos la tarea del watchdog por completo
// durante esta llamada (esp_task_wdt_delete), pero eso significaba que si
// el radio de WiFi quedaba colgado de verdad (visto con hardware real:
// pantalla negra permanente, ni siquiera levanta la red "QRcade_Config"),
// no había NADA que recuperara la placa sin desenchufarla a mano. Ahora en
// cambio alargamos el timeout en vez de sacar la protección del todo.
static const uint32_t WDT_TIMEOUT_PORTAL_S = 90;

// Si el WiFi está caído más de esto de forma continua, reinicio completo.
static const unsigned long WIFI_DOWN_RESTART_MS = 5UL * 60UL * 1000UL; // 5 min

// Auto-reinicio preventivo por uptime, para limpiar heap fragmentado antes
// de que sea un problema. 12hs es un buen equilibrio entre "no molestar" y
// "no dejar que se acumule fragmentación".
static const unsigned long RESTART_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL; // 12 h

// Si el heap libre cae debajo de esto, reinicio preventivo en el próximo
// punto seguro (nunca a mitad de un pulso de ficha).
static const uint32_t HEAP_SAFETY_THRESHOLD = 20000; // bytes

// ============================================================================
//                          ESTADO DE CHECK-IN
// ============================================================================
Preferences preferences;

static const unsigned long CHECKIN_INTERVAL_MS = 5000;
unsigned long lastCheckinAt = 0;
unsigned long lastWifiOkAt = 0;
unsigned long bootAt = 0;
bool pendingSafeRestart = false;

String deviceStatus = "";     // "unclaimed" | "claimed" | "revoked" | "" (sin respuesta aún)
String deviceMode = "fijo";   // "fijo" | "combo" — sólo relevante en estado "claimed"
String claimCode = "";        // sólo relevante en estado "unclaimed"
bool devicePaused = false;
String cachedQrData = "";

// Métricas simples para diagnóstico por Serial (no se mandan al backend).
uint32_t checkinOkCount = 0;
uint32_t checkinFailCount = 0;

// ============================================================================
//                    ESTADO DE COMBOS (modo "monto abierto")
// ============================================================================
// Menú monto->fichas que el dueño configuró desde el panel. Se recibe y
// cachea igual (por si a futuro se vuelve a mostrar en pantalla o se usa
// para otra cosa), pero YA NO se dibuja en la pantallita — el dueño pega
// stickers físicos con los montos al lado de la placa (pedido explícito,
// sept. 2026). La pantalla en modo combo queda igual que en modo fijo:
// sólo el QR a pantalla completa (ver actualizarPantalla / dibujarQR).
struct FichaCombo {
  float monto;
  int fichas;
};
static const int MAX_COMBOS = 6;
FichaCombo cachedCombos[MAX_COMBOS];
int cachedComboCount = 0;

// Hash chico del contenido de los combos (no criptográfico, sólo para
// detectar cambios) — así sabemos si hace falta forzar un redibujado de
// pantalla aunque el estado (SHOW_QR) no haya cambiado. Sin esto, si el
// dueño edita los combos desde el panel mientras la placa está prendida,
// la pantalla se quedaría mostrando la lista vieja hasta el próximo
// cambio de estado real (o un reinicio).
uint32_t cachedComboHash = 0;
uint32_t lastDrawnComboHash = 0xFFFFFFFF; // distinto de cualquier hash real -> fuerza el primer dibujado
bool forzarRedibujado = false;

// ============================================================================
//                          ESTADO DE PANTALLA
// ============================================================================
enum DisplayState {
  SHOW_BOOT,
  SHOW_QR,
  SHOW_IDLE,
  SHOW_PAID,
  SHOW_OFFLINE,
  SHOW_UNCLAIMED,
  SHOW_UNCONFIGURED,
  SHOW_PAUSED,
  SHOW_REVOKED,
  SHOW_CONFIG_PORTAL
};
DisplayState currentState = SHOW_BOOT;
DisplayState previousState = SHOW_BOOT;
unsigned long displayStateStart = 0;

static const unsigned long QR_DURATION_MS = 6000;
static const unsigned long IDLE_DURATION_MS = 2000;
static const unsigned long PAID_DURATION_MS = 4000;

// ============================================================================
//                          WIFIMANAGER (sólo bajo demanda)
// ============================================================================
WiFiManager wm;

// ============================================================================
//                    PERSISTENCIA MANUAL DE CREDENCIALES WIFI
// ============================================================================
// El guardado nativo (WiFi.persistent + NVS del propio driver) resultó poco
// confiable en esta combinación (WiFiManager con el portal en modo no
// bloqueante): incluso forzando WiFi.persistent(true) + WiFi.begin(ssid,pass)
// justo después de un portal exitoso, se confirmó con hardware real que las
// credenciales NO sobreviven un apagado y encendido real (la placa vuelve
// directo al portal). Para dejar de depender de esa persistencia nativa,
// guardamos nosotros mismos el SSID/password en un namespace propio de
// Preferences (que usa NVS por debajo, pero con nuestro propio control total
// de cuándo se escribe y se lee) y los usamos como única fuente de verdad
// para reconectar al arrancar.
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
  wifiPrefs.begin(WIFI_PREFS_NS, true); // sólo lectura
  ssidOut = wifiPrefs.getString(WIFI_PREFS_KEY_SSID, "");
  passOut = wifiPrefs.getString(WIFI_PREFS_KEY_PASS, "");
  wifiPrefs.end();
  return ssidOut.length() > 0;
}

// ============================================================================
//                          UTILIDADES DE LOG
// ============================================================================
void logLine(const String &s) {
  if (DEBUG_LOG) Serial.println(s);
}

// ============================================================================
//                          RELÉ
// ============================================================================
void triggerCoinPulse() {
  logLine(">> Disparando rele...");
  digitalWrite(RELAY_PIN, RELAY_ACTIVE);
  delay(PULSE_DURATION_MS);
  digitalWrite(RELAY_PIN, RELAY_INACTIVE);
  logLine(">> Pulso completado.");
}

void triggerCoinPulses(int veces) {
  if (veces < 1) veces = 1;
  if (veces > 20) veces = 20; // límite de cordura, nunca confiar ciegamente en el backend
  for (int i = 0; i < veces; i++) {
    triggerCoinPulse();
    esp_task_wdt_reset();
    if (i < veces - 1) delay(250);
  }
}

// ============================================================================
//                          PANTALLA
// ============================================================================
void centrarTexto(const String &texto, int y, uint16_t color, uint8_t size) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(texto, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  gfx->setCursor(x, y);
  gfx->print(texto);
}

static const uint16_t QR_BYTE_CAPACITY_ECC_LOW[] = {
    17, 32, 53, 78, 106, 134, 154, 192, 230, 271,
   321, 367, 425, 458, 520, 586, 644, 718, 792, 858
};
#define QR_VERSION_MAX 20

int versionMinimaNecesaria(uint16_t largoDato) {
  for (int v = 1; v <= QR_VERSION_MAX; v++) {
    if (QR_BYTE_CAPACITY_ECC_LOW[v - 1] >= largoDato) return v;
  }
  return 0;
}

void dibujarQR(const String &data) {
  gfx->fillScreen(COLOR_BLANCO);

  if (data.length() == 0) {
    centrarTexto("Sin datos", SCREEN_HEIGHT / 2, COLOR_NEGRO, 2);
    return;
  }

  int versionUsada = versionMinimaNecesaria(data.length());
  if (versionUsada == 0) {
    logLine("!! QR: dato demasiado largo.");
    centrarTexto("Error QR", SCREEN_HEIGHT / 2, COLOR_NEGRO, 2);
    return;
  }

  uint8_t qrcodeData[qrcode_getBufferSize(versionUsada)];
  QRCode qrcode;
  int8_t resultado = qrcode_initText(&qrcode, qrcodeData, versionUsada, ECC_LOW, data.c_str());

  if (resultado != 0) {
    logLine("!! QR: error generando (codigo " + String(resultado) + ").");
    centrarTexto("Error QR", SCREEN_HEIGHT / 2, COLOR_NEGRO, 2);
    return;
  }

  const int QUIET_ZONE_MODULES = 4;
  int totalModulosConZona = qrcode.size + QUIET_ZONE_MODULES * 2;
  int pixelPorModulo = SCREEN_WIDTH / totalModulosConZona;
  if (pixelPorModulo < 1) pixelPorModulo = 1;

  int totalPxQR = pixelPorModulo * qrcode.size;
  int offsetX = (SCREEN_WIDTH - totalPxQR) / 2;
  int offsetY = (SCREEN_HEIGHT - totalPxQR) / 2;

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        gfx->fillRect(offsetX + x * pixelPorModulo, offsetY + y * pixelPorModulo,
                     pixelPorModulo, pixelPorModulo, COLOR_NEGRO);
      }
    }
  }
}

// Dibuja el QR en un cuadro acotado a maxSizePx (en vez de la pantalla
// entera), arrancando en offsetYTop — para dejar lugar debajo a la lista
// de combos. Devuelve el lado real en pixeles que ocupó el cuadro
// (incluida su zona de quiet blanca), para que el texto de abajo sepa
// desde dónde arrancar sin superponerse.
int dibujarQRAcotado(const String &data, int maxSizePx, int offsetYTop) {
  if (data.length() == 0) return 0;

  int versionUsada = versionMinimaNecesaria(data.length());
  if (versionUsada == 0) {
    logLine("!! QR: dato demasiado largo.");
    return 0;
  }

  uint8_t qrcodeData[qrcode_getBufferSize(versionUsada)];
  QRCode qrcode;
  int8_t resultado = qrcode_initText(&qrcode, qrcodeData, versionUsada, ECC_LOW, data.c_str());
  if (resultado != 0) {
    logLine("!! QR: error generando (codigo " + String(resultado) + ").");
    return 0;
  }

  const int QUIET_ZONE_MODULES = 2;
  int totalModulosConZona = qrcode.size + QUIET_ZONE_MODULES * 2;
  int pixelPorModulo = maxSizePx / totalModulosConZona;
  if (pixelPorModulo < 1) pixelPorModulo = 1;

  int totalPxQR = pixelPorModulo * qrcode.size;
  int cuadroLado = totalPxQR + pixelPorModulo * QUIET_ZONE_MODULES * 2;
  int cuadroX = (SCREEN_WIDTH - cuadroLado) / 2;

  // Fondo blanco sólo del cuadro del QR (con su zona de quiet), no de toda
  // la pantalla — el resto de la pantalla ya se limpió en negro en
  // dibujarQRConCombos() antes de llamar acá.
  gfx->fillRect(cuadroX, offsetYTop, cuadroLado, cuadroLado, COLOR_BLANCO);

  int offsetX = (SCREEN_WIDTH - totalPxQR) / 2;
  int offsetY = offsetYTop + pixelPorModulo * QUIET_ZONE_MODULES;

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        gfx->fillRect(offsetX + x * pixelPorModulo, offsetY + y * pixelPorModulo,
                     pixelPorModulo, pixelPorModulo, COLOR_NEGRO);
      }
    }
  }

  return cuadroLado;
}

// Pantalla de modo "combo" (monto abierto): QR más chico arriba + lista
// "$monto -> N ficha(s)" debajo, para que el cliente sepa qué monto
// escanear y pagar. Si todavía no hay ningún combo cargado desde el
// panel, se cae al QR de siempre a pantalla completa — mejor mostrar
// algo que nada, aunque sin combos configurados ningún pago va a
// acreditar fichas (ver webhook).
void dibujarQRConCombos(const String &data, FichaCombo *combos, int cantidad) {
  if (cantidad == 0) {
    dibujarQR(data);
    return;
  }

  gfx->fillScreen(COLOR_NEGRO);

  int ladoQR = dibujarQRAcotado(data, 148, 8);
  if (ladoQR == 0) {
    centrarTexto("Error QR", SCREEN_HEIGHT / 2, COLOR_BLANCO, 2);
    return;
  }

  int y = 8 + ladoQR + 10;
  centrarTexto("Monto -> fichas", y, COLOR_AZUL, 1);
  y += 15;

  for (int i = 0; i < cantidad && y <= SCREEN_HEIGHT - 12; i++) {
    // $ sin decimales para que entre cómodo en una sola línea a este
    // tamaño de fuente — los montos de fichas siempre son números redondos.
    char linea[40];
    snprintf(linea, sizeof(linea), "$%d -> %d ficha%s",
             (int)(combos[i].monto + 0.5f), combos[i].fichas,
             combos[i].fichas == 1 ? "" : "s");
    centrarTexto(String(linea), y, COLOR_BLANCO, 1);
    y += 14;
  }
}

void dibujarPantallaEspera() {
  gfx->fillScreen(COLOR_NEGRO);
  centrarTexto("Escanea y Juga!", SCREEN_HEIGHT / 2 - 10, COLOR_BLANCO, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 34, COLOR_AZUL, 2);
}

void dibujarPantallaPagoOk() {
  gfx->fillScreen(COLOR_VERDE);
  centrarTexto("Pago", SCREEN_HEIGHT / 2 - 30, COLOR_BLANCO, 3);
  centrarTexto("Exitoso!", SCREEN_HEIGHT / 2 + 10, COLOR_BLANCO, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 34, COLOR_AZUL, 2);
}

void dibujarPantallaOffline() {
  gfx->fillScreen(COLOR_ROJO);
  centrarTexto("Fuera de", SCREEN_HEIGHT / 2 - 30, COLOR_BLANCO, 2);
  centrarTexto("Servicio", SCREEN_HEIGHT / 2, COLOR_BLANCO, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 34, COLOR_AZUL, 2);
}

void dibujarPantallaBoot(const String &msg) {
  gfx->fillScreen(COLOR_NEGRO);
  centrarTexto("QRcade", SCREEN_HEIGHT / 2 - 20, COLOR_AZUL, 2);
  centrarTexto(msg, SCREEN_HEIGHT / 2 + 20, COLOR_BLANCO, 1);
}

void dibujarPantallaSinReclamar() {
  gfx->fillScreen(COLOR_NEGRO);
  centrarTexto("Esperando", SCREEN_HEIGHT / 2 - 30, COLOR_BLANCO, 2);
  centrarTexto("activacion...", SCREEN_HEIGHT / 2, COLOR_BLANCO, 2);
  if (claimCode.length() > 0) {
    centrarTexto("Codigo: " + claimCode, SCREEN_HEIGHT / 2 + 30, COLOR_AMARILLO, 1);
  }
  centrarTexto("QRcade", SCREEN_HEIGHT - 20, COLOR_AZUL, 2);
}

void dibujarPantallaSinConfigurar() {
  gfx->fillScreen(COLOR_GRIS);
  centrarTexto("Cobro sin", SCREEN_HEIGHT / 2 - 20, COLOR_NEGRO, 2);
  centrarTexto("configurar", SCREEN_HEIGHT / 2 + 10, COLOR_NEGRO, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 34, COLOR_AZUL, 2);
}

void dibujarPantallaPausada() {
  gfx->fillScreen(COLOR_NEGRO);
  centrarTexto("Maquina en", SCREEN_HEIGHT / 2 - 20, COLOR_GRIS, 2);
  centrarTexto("pausa", SCREEN_HEIGHT / 2 + 10, COLOR_GRIS, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 34, COLOR_AZUL, 2);
}

void dibujarPantallaRevocada() {
  gfx->fillScreen(COLOR_ROJO);
  centrarTexto("Placa dada de", SCREEN_HEIGHT / 2 - 20, COLOR_BLANCO, 1);
  centrarTexto("baja", SCREEN_HEIGHT / 2, COLOR_BLANCO, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 34, COLOR_AZUL, 2);
}

void dibujarPantallaConfigPortal() {
  gfx->fillScreen(COLOR_AZUL);
  centrarTexto("Modo config.", SCREEN_HEIGHT / 2 - 30, COLOR_BLANCO, 2);
  centrarTexto("Conectate a:", SCREEN_HEIGHT / 2, COLOR_BLANCO, 1);
  centrarTexto("QRcade_Config", SCREEN_HEIGHT / 2 + 20, COLOR_AMARILLO, 1);
}

// OJO: sin valor por default acá — el Arduino IDE autogenera un prototipo a
// partir de esta misma línea, y si el default queda tanto en el prototipo
// autogenerado como en esta definición, C++ lo rechaza como "redefinido".
// Por eso los dos únicos llamados sin forzar pasan explícitamente `false`.
void actualizarPantalla(bool forzar) {
  if (currentState == previousState && !forzar) return;

  switch (currentState) {
    case SHOW_QR:
      // Pedido explícito del dueño: en modo combo también se muestra el QR
      // a pantalla completa, igual que en modo fijo — sin la lista de
      // montos en pantalla (ilegible en la pantallita chica). El dueño
      // pega stickers físicos con los montos/fichas al lado de la placa.
      dibujarQR(cachedQrData);
      break;
    case SHOW_IDLE:            dibujarPantallaEspera(); break;
    case SHOW_PAID:            dibujarPantallaPagoOk(); break;
    case SHOW_OFFLINE:         dibujarPantallaOffline(); break;
    case SHOW_UNCLAIMED:       dibujarPantallaSinReclamar(); break;
    case SHOW_UNCONFIGURED:    dibujarPantallaSinConfigurar(); break;
    case SHOW_PAUSED:          dibujarPantallaPausada(); break;
    case SHOW_REVOKED:         dibujarPantallaRevocada(); break;
    case SHOW_CONFIG_PORTAL:   dibujarPantallaConfigPortal(); break;
    case SHOW_BOOT:            dibujarPantallaBoot("Iniciando..."); break;
  }
  previousState = currentState;
}

// Decide qué pantalla corresponde según el último check-in conocido.
void resolverEstadoSegunBackend() {
  DisplayState objetivo;

  if (deviceStatus == "revoked") {
    objetivo = SHOW_REVOKED;
  } else if (deviceStatus == "unclaimed") {
    objetivo = SHOW_UNCLAIMED;
  } else if (deviceStatus == "claimed") {
    if (devicePaused) {
      objetivo = SHOW_PAUSED;
    } else if (cachedQrData == "") {
      objetivo = SHOW_UNCONFIGURED;
    } else {
      // Pedido explícito: una vez configurada, siempre el QR fijo — ya no
      // se cicla a "Escaneá y Jugá" ni se corta con "Pago Exitoso".
      objetivo = SHOW_QR;
    }
  } else {
    objetivo = SHOW_BOOT;
  }

  bool comboMenuCambio = (deviceMode == "combo" && cachedComboHash != lastDrawnComboHash);

  if (objetivo != currentState) {
    currentState = objetivo;
    displayStateStart = millis();
  }

  if (comboMenuCambio && currentState == SHOW_QR) {
    lastDrawnComboHash = cachedComboHash;
    forzarRedibujado = true;
  }
}

// ============================================================================
//                    CHECK-IN CONTRA NUESTRO BACKEND (socket TLS crudo)
// ============================================================================
// Nunca usa HTTPClient a propósito: acá controlamos cada timeout a mano
// (conectar / esperar respuesta / leer body) para que, pase lo que pase del
// otro lado, esta función SIEMPRE vuelve en menos de ~12s. Eso es lo que
// permite que el watchdog de 30s nunca tenga que intervenir en operación
// normal, y que un backend caído no tilde la placa.
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

  // Salteamos los headers HTTP hasta la línea en blanco.
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
  payload.reserve(2560); // la respuesta ahora puede traer firmware_update y/o combos
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
      // Ya no cambiamos de pantalla acá: el pedido es que la pantalla se
      // quede siempre en el QR fijo, sin cortar con "Pago Exitoso". El
      // relé igual se dispara normalmente.
      triggerCoinPulses(1);
      // El pico de corriente al enganchar la bobina puede hacer temblar
      // por una fracción de segundo el riel que comparte con la pantalla
      // (ambos en 3V3) y dejarla con basura en el buffer. En vez de
      // depender de que nunca tiemble, forzamos un redibujado completo
      // apenas termina el pulso — así, si hubo algún glitch momentáneo,
      // se "autocura" solo en cada dispensado.
      actualizarPantalla(true);
    } else if (comando.startsWith("dispense:")) {
      int veces = comando.substring(9).toInt();
      triggerCoinPulses(veces > 0 ? veces : 1);
      actualizarPantalla(true);
    } else {
      logLine("!! Comando desconocido, se ignora: " + comando);
    }
  }
}

// ============================================================================
//              ACTUALIZACIÓN REMOTA DE FIRMWARE (OTA)
// ============================================================================
// El backend sólo manda "firmware_update" en la respuesta del check-in
// cuando el dueño aceptó una versión nueva desde el panel (ver
// resolverFirmwareUpdate() en api/device/checkin). Bajamos el .bin con
// HTTPUpdate (misma lógica TLS "insecure" que el check-in — self-signed no
// aplica acá, es sólo para no lidiar con el certificado del CDN) y, si sale
// bien, HTTPUpdate reinicia la placa sola con el nuevo firmware adentro. El
// backend se entera de que quedó instalado en el PRÓXIMO check-in, cuando
// ve que firmware_version ya coincide con la versión que esperaba.
//
// OJO placa por placa: esto requiere que el Partition Scheme elegido en
// Arduino IDE (Tools > Partition Scheme) tenga DOS particiones de app (OTA
// habilitado) — el típico "Default 4MB with spiffs" sirve; "Huge APP" NO,
// porque le da toda la flash a una sola partición y no deja lugar para la
// copia nueva mientras se descarga. Si Update.begin() falla por falta de
// espacio, va a quedar bien clarito en el log de abajo. OJO 2 (confirmado
// con hardware real, sept. 2026): esto no alcanza con sólo tener el scheme
// correcto SELECCIONADO en el IDE — la tabla de particiones física del
// chip sólo se reescribe con un flasheo COMPLETO por USB (que graba
// bootloader + particiones + app en un solo paso). Si una placa se flasheó
// alguna vez con un scheme distinto, el OTA le va a fallar silenciosamente
// una y otra vez hasta que se la reflashee por USB una vez con el scheme
// correcto — a partir de ahí el OTA queda sano para siempre.
bool actualizacionFirmwareEnCurso = false;

void ejecutarActualizacionFirmware(const String &version, const String &binUrl) {
  if (actualizacionFirmwareEnCurso) return; // por las dudas, nunca solapar
  actualizacionFirmwareEnCurso = true;

  logLine(">> Firmware: actualizacion disponible (" + version + "). Descargando...");
  logLine(">> Firmware: " + binUrl);

  currentState = SHOW_BOOT;
  previousState = SHOW_UNCLAIMED;
  dibujarPantallaBoot("Actualizando...");

  // Igual que con el portal de config: esto es bloqueante y puede tardar
  // más que el timeout del watchdog en una red lenta, así que sacamos esta
  // tarea del watchdog mientras dura la descarga+flasheo.
  esp_task_wdt_delete(NULL);

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(false); // reiniciamos nosotros, después de loguear el resultado

  t_httpUpdate_return resultado = httpUpdate.update(client, binUrl);

  esp_task_wdt_add(NULL);

  switch (resultado) {
    case HTTP_UPDATE_FAILED:
      logLine("!! Firmware: fallo la actualizacion (" + String(httpUpdate.getLastErrorString().c_str()) + "). Sigo con la version actual.");
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
    return;
  }

  if (status == "claimed") {
    devicePaused = doc["is_paused"] | false;
    deviceMode = String((const char *)(doc["mode"] | "fijo"));
    const char *qr = doc["qr_data"] | (const char *)nullptr;
    cachedQrData = (qr != nullptr) ? String(qr) : "";

    // Menú de combos (sólo viene cuando mode == "combo") — se recalcula
    // completo en cada check-in y se compara por hash contra lo último
    // dibujado (ver resolverEstadoSegunBackend) para saber si hace falta
    // redibujar la pantalla.
    cachedComboCount = 0;
    uint32_t nuevoHash = 0;
    if (deviceMode == "combo" && doc["combos"].is<JsonArray>()) {
      JsonArray combosArr = doc["combos"].as<JsonArray>();
      for (JsonVariant c : combosArr) {
        if (cachedComboCount >= MAX_COMBOS) break;
        float monto = c["monto"] | 0.0;
        int fichas = c["fichas"] | 0;
        cachedCombos[cachedComboCount].monto = monto;
        cachedCombos[cachedComboCount].fichas = fichas;
        cachedComboCount++;
        nuevoHash = nuevoHash * 31 + (uint32_t)(monto * 100.0f);
        nuevoHash = nuevoHash * 31 + (uint32_t)fichas;
      }
    }
    cachedComboHash = nuevoHash;

    if (doc["commands"].is<JsonArray>()) {
      procesarComandos(doc["commands"].as<JsonArray>());
    }

    // Se procesa DESPUÉS de los comandos y devuelve el control recién
    // cuando termina (éxito, falla, o reinicio) — nunca hay un check-in
    // superpuesto con una descarga en curso.
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
// Sólo dispara un reinicio "en un punto seguro": nunca en medio de un pulso
// de relé, y siempre con log previo para que quede constancia en Serial de
// por qué se reinició (si alguien está mirando el monitor en ese momento).
void chequearSaludYReiniciarSiHaceFalta() {
  uint32_t freeHeap = ESP.getFreeHeap();

  if (freeHeap < HEAP_SAFETY_THRESHOLD) {
    logLine("!! Heap bajo (" + String(freeHeap) + " bytes libres). Reinicio preventivo.");
    delay(200);
    ESP.restart();
  }

  if (millis() - bootAt > RESTART_INTERVAL_MS) {
    logLine(">> Reinicio preventivo por uptime (" + String(RESTART_INTERVAL_MS / 3600000UL) + "h).");
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

// Reconfigura el timeout del Task Watchdog sin sacar la tarea actual de su
// vigilancia (a diferencia de esp_task_wdt_delete/_add, que la deja sin
// ninguna protección mientras tanto). Misma bifurcación 2.x/3.x que en
// setup().
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
//                    PORTAL DE CONFIGURACION (bajo demanda, NUNCA automático)
// ============================================================================
// Sólo se llama en dos casos: no hay credenciales guardadas, o el usuario
// mantuvo apretado el botón BOOT al encender. Es bloqueante a propósito —
// mientras el portal está activo la placa no hace check-in ni polling de
// ningún tipo, así que el combo "doble radio + TLS" que colgaba los
// firmwares viejos ya no puede pasar: durante el portal no hay TLS, y
// durante operación normal no hay portal.
void abrirPortalDeConfiguracion() {
  currentState = SHOW_CONFIG_PORTAL;
  previousState = SHOW_BOOT;
  actualizarPantalla(false);

  // IMPORTANTE (parte 1): wm.startConfigPortal() en modo bloqueante corre su
  // propio loop interno durante todo el tiempo que el portal está abierto, y
  // nosotros no tenemos forma de meter un esp_task_wdt_reset() ahí adentro.
  // Por eso lo manejamos en modo no bloqueante con nuestro propio loop más
  // abajo, alimentando el watchdog en cada vuelta.
  //
  // IMPORTANTE (parte 2, la que faltaba): incluso en modo no bloqueante, la
  // PROPIA LLAMADA a startConfigPortal() no es instantánea — internamente
  // hace un WiFi.scanNetworks() sincrónico para poblar la lista de redes de
  // la página del portal, y ese escaneo por sí solo puede tardar más que
  // WDT_TIMEOUT_S (confirmado con hardware real: reinicios en bucle
  // mostrando "Starting Web Portal" y nada más, justo el tiempo del
  // watchdog).
  //
  // IMPORTANTE (parte 3 — bug real de campo, sept. 2026): cuando esta
  // función se llama porque el WiFi guardado NO se encontró (router
  // apagado/desenchufado, no que esté mal la contraseña), el radio viene de
  // 20s reintentando WiFi.begin() contra una red que no está — y arrancar
  // el modo AP+STA de wm.startConfigPortal() justo después, con el radio en
  // ese estado, puede dejar el driver de WiFi del ESP32 colgado para
  // siempre (confirmado con hardware real: pantalla completamente negra, ni
  // siquiera llega a levantar la red "QRcade_Config" para conectarse).
  // Antes además sacábamos esta tarea del watchdog por completo durante la
  // llamada (esp_task_wdt_delete), así que ni el propio watchdog rescataba
  // a la placa de ese cuelgue — quedaba tildada hasta desenchufarla a mano.
  // Dos cambios para esto:
  //   1) Apagar y volver a prender el radio (WiFi.mode(WIFI_OFF) y de
  //      nuevo WIFI_STA) ANTES de abrir el portal, para que
  //      startConfigPortal() arranque siempre de un estado limpio en vez
  //      de heredar el de un WiFi.begin() fallido.
  //   2) Ya no sacamos la tarea del watchdog del todo — la dejamos con un
  //      timeout más largo (WDT_TIMEOUT_PORTAL_S) que sí alcanza para un
  //      escaneo lento normal, pero que igual reinicia la placa sola si el
  //      radio se cuelga de verdad, en vez de dejarla negra para siempre.
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(100);

  configurarWatchdog(WDT_TIMEOUT_PORTAL_S);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0); // el timeout real lo controlamos abajo
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

  // OJO — el bug de fondo detrás de "no reconecta sola tras apagar y
  // prender": con el portal en modo no bloqueante, WiFiManager a veces NO
  // termina grabando las credenciales nuevas en la memoria persistente
  // (NVS) de la ESP32 — quedan sólo en RAM para esta sesión. Ya se probó
  // forzar WiFi.persistent(true) + WiFi.begin(WiFi.SSID(), WiFi.psk()) acá
  // mismo y NO alcanzó: confirmado con hardware real que la placa sigue
  // "olvidándose" tras un apagado/encendido real. Dejamos de confiar por
  // completo en la persistencia nativa del driver/WiFiManager y guardamos
  // las credenciales nosotros mismos, a mano, en nuestro propio namespace
  // de Preferences — usando los getters dedicados de WiFiManager
  // (más confiables que WiFi.SSID()/WiFi.psk() para leer lo que se acaba
  // de configurar en el portal).
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

// ============================================================================
//                              SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  // Watchdog primero que nada: si algo de lo que sigue se cuelga, el chip
  // se recupera solo. La firma de esp_task_wdt_init() cambió entre el core
  // ESP32 2.x (dos parámetros sueltos) y el 3.x (una struct de config) —
  // contemplamos las dos para que compile en cualquiera de los dos.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_err_t wdtInitErr = esp_task_wdt_init(&twdt_config);
  if (wdtInitErr == ESP_ERR_INVALID_STATE) {
    // El core de Arduino ya lo había inicializado con su propio default;
    // sólo lo reconfiguramos con nuestro timeout.
    esp_task_wdt_reconfigure(&twdt_config);
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);

  // Relé siempre arranca inactivo, sin excepciones.
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_INACTIVE);

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(50);
  digitalWrite(TFT_RST, LOW);  delay(100);
  digitalWrite(TFT_RST, HIGH); delay(100);

  gfx->begin();
  dibujarPantallaBoot("Iniciando...");
  previousState = SHOW_UNCLAIMED; // fuerza primer redibujado real más abajo

  // OJO, este fue uno de los bugs detrás de "no reconecta sola al WiFi
  // guardado": en el ESP32, WiFi.macAddress() sólo devuelve datos reales
  // una vez que el driver de WiFi arrancó (WiFi.mode(...)). Por eso hay que
  // poner el modo STA acá arriba, antes de leer nada.
  WiFi.mode(WIFI_STA);
  delay(100); // le da tiempo al driver de WiFi a terminar de inicializar
              // antes de confiar en macAddress() de acá abajo.

  logLine("");
  logLine("========================================");
  logLine("QRcade firmware v" + String(FIRMWARE_VERSION));
  logLine("MAC: " + WiFi.macAddress());
  logLine("========================================");

  bool pedirPortal = botonConfigMantenidoAlBoot();
  if (pedirPortal) {
    logLine(">> Boton de configuracion detectado al arrancar.");
  }

  esp_task_wdt_reset();

  // Ya no confiamos en WiFi.SSID() (lectura de lo que el driver nativo haya
  // persistido, poco confiable en este setup) — leemos nuestras propias
  // credenciales guardadas a mano en Preferences (ver
  // guardarCredencialesWifi()/leerCredencialesWifi() más arriba).
  String ssidGuardado, passGuardado;
  bool hayCredenciales = leerCredencialesWifi(ssidGuardado, passGuardado);
  if (hayCredenciales) {
    logLine(">> Credenciales guardadas encontradas para SSID: " + ssidGuardado);
  } else {
    logLine(">> No hay credenciales guardadas en Preferences.");
  }

  if (pedirPortal || !hayCredenciales) {
    abrirPortalDeConfiguracion();
    // abrirPortalDeConfiguracion() sólo retorna si conectó con éxito.
  } else {
    dibujarPantallaBoot("Conectando WiFi...");
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

  dibujarPantallaBoot("Sincronizando...");
  hacerCheckin();
  resolverEstadoSegunBackend();
  previousState = SHOW_BOOT; // fuerza a que el primer estado real se dibuje

  esp_task_wdt_reset();
}

// ============================================================================
//                               LOOP
// ============================================================================
void loop() {
  esp_task_wdt_reset();

  if (WiFi.status() != WL_CONNECTED) {
    if (currentState != SHOW_OFFLINE) {
      currentState = SHOW_OFFLINE;
      displayStateStart = millis();
    }
    // Reconexión activa, no bloqueante: el propio WiFi.reconnect() dispara
    // el intento y seguimos con el loop; la salud general (por si nunca
    // vuelve) la cubre chequearSaludYReiniciarSiHaceFalta().
    static unsigned long lastReconnectTry = 0;
    if (millis() - lastReconnectTry > 10000) {
      lastReconnectTry = millis();
      logLine("!! WiFi desconectado, reintentando...");
      WiFi.reconnect();
    }
  } else {
    if (currentState == SHOW_OFFLINE) {
      currentState = SHOW_BOOT;
      displayStateStart = millis();
    }

    if (millis() - lastCheckinAt >= CHECKIN_INTERVAL_MS) {
      lastCheckinAt = millis();
      hacerCheckin();
      resolverEstadoSegunBackend();
    }

    // Pedido explícito: la pantalla NO cicla más entre QR y "Escaneá y
    // Jugá", y tampoco corta al QR con el cartel de "Pago Exitoso" — una
    // vez configurada, se queda siempre mostrando el QR fijo (o el QR +
    // menú de combos, en modo "combo"). Por eso ya no hay nada que hacer
    // acá con QR_DURATION_MS/IDLE_DURATION_MS/PAID_DURATION_MS (quedan
    // declaradas pero sin uso, no hace falta borrarlas). El estado lo
    // sigue decidiendo resolverEstadoSegunBackend().
  }

  actualizarPantalla(forzarRedibujado);
  forzarRedibujado = false;
  chequearSaludYReiniciarSiHaceFalta();

  if (Serial.available() > 0) {
    char key = Serial.read();
    if (key == 'c' || key == 'C') {
      triggerCoinPulse();
    } else if (key == 'h' || key == 'H') {
      Serial.println("Heap libre: " + String(ESP.getFreeHeap()) + " bytes");
      Serial.println("Check-ins OK: " + String(checkinOkCount) + "  fallidos: " + String(checkinFailCount));
      Serial.println("Uptime: " + String((millis() - bootAt) / 1000) + "s");
    }
  }

  delay(10);
}
