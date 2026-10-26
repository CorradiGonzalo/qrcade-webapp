/*
  ============================================================================
  QRcade — Firmware "display" v1.0.0 (pantalla externa para cafeteras)
  Placa: Guition JC3248W535 — ESP32-S3, pantalla IPS 3.5" 320x480,
  controlador AXS15231B por bus QSPI (NO es SPI simple de 4 cables como la
  pantallita de la garra — son placas y drivers totalmente distintos).
  ============================================================================
  Placa COMPAÑERA de una cafetera ya armada con su propia placa de cobro
  (qrcade_cafetera_v1.0.0.ino — esa es la que tiene el relé, procesa pagos
  de Mercado Pago y NO SE TOCA). Esta placa es sólo una pantalla:

    - NO tiene relé ni ningún pin de salida de potencia.
    - NO habla con Mercado Pago.
    - NO hace /api/device/checkin, no tiene factory_key, no se "reclama"
      desde el panel como una máquina nueva.
    - Lo ÚNICO que hace es pedir, cada pocos segundos, el QR vigente de
      la cafetera a la que está asociada (vía GET /api/display/{codigo}/qr)
      y dibujarlo en pantalla. El aviso de pago le llega a la OTRA placa
      (la que cobra); para el panel, sigue siendo una sola máquina.

  El "código de emparejamiento" (8 caracteres, generado desde el panel en
  la página de la máquina — ver DisplayPairingCard.tsx / migración 0014)
  es lo único que vincula esta pantalla con la cafetera correspondiente.
  No es secreto como el factory_key: sólo deja LEER el QR de esa máquina,
  nunca escribir nada. Se carga una sola vez por el portal WiFi (campo
  extra) y queda guardado en Preferences.

  ----------------------------------------------------------------------------
  HARDWARE — Guition JC3248W535 (confirmado contra el repo de referencia
  me-processware/JC3248W535-Driver y guías de la comunidad, oct. 2026):

    Bus QSPI de la pantalla:
      CS=45  SCK=47  D0=21  D1=48  D2=40  D3=39
    Backlight:        GPIO 1  (activo en HIGH, sin PWM)
    Reset de panel:   no tiene pin propio en este modelo (GFX_NOT_DEFINED)
    Resolución:       320x480 (se usa en orientación nativa, sin rotar —
                       el panel ignora el comando de ventana en algunos
                       lotes, así que evitamos setRotation() y dibujamos
                       todo "derecho" como viene de fábrica)
    Touch (no usado):  I2C SDA=4 SCL=8 INT=11 RST=12 addr 0x3B — esta
                       placa sólo muestra el QR, no necesita touch, así
                       que no se inicializa (evita además un conflicto
                       conocido: el pin de reset del touch en algunas
                       placas comparte el SCLK de la ranura SD).

  Librería: GFX Library for Arduino (moononournation), versión 1.5.0 o
  superior — las versiones viejas NO tienen la clase Arduino_AXS15231B.
  Si ya la tenías instalada para la garra/cafetera, actualizala desde
  Library Manager antes de compilar este sketch.

  Esta pantalla usa un FRAMEBUFFER en RAM (Arduino_Canvas) porque el panel
  QSPI no deja escribir ventanas parciales de forma confiable en todos los
  lotes — se dibuja todo en memoria y se manda de una sola vez a pantalla
  con gfx->flush(). Esto necesita PSRAM (la placa trae 8MB, de sobra).

  Configuración en Arduino IDE (Tools):
    - Board:              "ESP32S3 Dev Module"
    - USB CDC On Boot:    "Enabled"   (si no, no vas a ver nada por Serial)
    - PSRAM:              "OPI PSRAM"
    - Flash Size:          16MB (128Mb)
    - Flash Mode:           QIO 80MHz
    - Partition Scheme:    cualquiera de 16MB alcanza de sobra (este
                            sketch no usa OTA ni SPIFFS grande)

  ----------------------------------------------------------------------------
  Por qué NO usa HTTPClient para pedir el QR: lección aprendida con la
  placa de cobro — HTTPClient en el ESP32 pide más memoria contigua de la
  que a veces hay disponible y puede devolver "no se pudo conectar" de
  forma intermitente aun con WiFi sano. Esta placa usa el mismo patrón de
  socket TLS crudo (WiFiClientSecure) con timeouts duros en cada paso, que
  ya se probó confiable en la placa de cobro.

  Librerías necesarias (Arduino Library Manager):
    - WiFiManager (tzapu)
    - ArduinoJson
    - GFX Library for Arduino (moononournation / Arduino_GFX_Library) 1.5.0+
    - QRCode (ricmoo) — se usa la copia local qrcode_impl.c/h, no hace
      falta instalarla aparte.
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
#include "qrcode_impl.h"

// ============================================================================
//                          CONFIG DE BACKEND
// ============================================================================
static const char *BACKEND_HOST = "qrcade.dev";
static const uint16_t BACKEND_PORT = 443;
// Se completa en tiempo de ejecución con "/api/display/" + codigo + "/qr".

static const char *FIRMWARE_VERSION = "display-1.0.0";

// ============================================================================
//                CONFIG DE HARDWARE — pantalla QSPI (JC3248W535)
// ============================================================================
#define LCD_CS   45
#define LCD_SCK  47
#define LCD_D0   21
#define LCD_D1   48
#define LCD_D2   40
#define LCD_D3   39
#define LCD_BL   1

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 480

// Esta placa no tiene un botón BOOT expuesto con el mismo uso que en las
// otras — en el ESP32-S3 el pin de boot/flash (GPIO0) SÍ existe físicamente
// (suele venir como botón "BOOT" o "IO0" en la placa) y se puede seguir
// usando igual para abrir el portal de configuración.
static const int CONFIG_BUTTON_PIN = 0;
static const unsigned long CONFIG_BUTTON_HOLD_MS = 2000;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED /* reset */, 0 /* rotation */, false /* ips */, SCREEN_WIDTH, SCREEN_HEIGHT);
// Framebuffer en RAM (PSRAM): el panel QSPI de esta placa no responde bien
// a escrituras parciales en todos los lotes, así que dibujamos todo en
// memoria y lo mandamos de una vez con gfx->flush().
Arduino_Canvas *gfx = new Arduino_Canvas(SCREEN_WIDTH, SCREEN_HEIGHT, panel, 0, 0, 0);

#define COLOR_NEGRO    0x0000
#define COLOR_BLANCO   0xFFFF
#define COLOR_VERDE    0x07E0
#define COLOR_ROJO     0xF800
#define COLOR_AZUL     0x051D
#define COLOR_GRIS     0x7BEF
#define COLOR_AMARILLO 0xFFE0

// ============================================================================
//                          BLINDAJE / HARDENING
// ============================================================================
static const bool DEBUG_LOG = true;
static const uint32_t WDT_TIMEOUT_S = 30;
static const uint32_t WDT_TIMEOUT_PORTAL_S = 90;
static const unsigned long WIFI_DOWN_RESTART_MS = 5UL * 60UL * 1000UL; // 5 min
static const unsigned long RESTART_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL; // 12 h
static const uint32_t HEAP_SAFETY_THRESHOLD = 20000; // bytes

// Cada cuánto se repide el QR vigente. Esta placa no cobra ni dispara
// nada por sí sola — no hay motivo para que sea tan rápida como el
// check-in de la placa de cobro; 15s es más que suficiente para que el
// QR de la pantalla se actualice sin demora perceptible para el cliente.
static const unsigned long POLL_INTERVAL_MS = 15000;

unsigned long lastPollAt = 0;
unsigned long lastWifiOkAt = 0;
unsigned long bootAt = 0;

uint32_t pollOkCount = 0;
uint32_t pollFailCount = 0;

// ============================================================================
//                          ESTADO DEL QR / MÁQUINA
// ============================================================================
// "" = todavía sin respuesta del backend. "ok" = último poll exitoso.
// "sin_codigo" = no hay código de emparejamiento cargado. "no_encontrado"
// = el backend devolvió 404 (código inválido o regenerado desde el panel).
String pollStatus = "";
String cachedQrData = "";
bool devicePaused = false;

// ============================================================================
//                          ESTADO DE PANTALLA
// ============================================================================
enum DisplayState {
  SHOW_BOOT,
  SHOW_QR,
  SHOW_OFFLINE,
  SHOW_PAUSADA,
  SHOW_SIN_CODIGO,
  SHOW_CODIGO_INVALIDO,
  SHOW_CONFIG_PORTAL
};
DisplayState currentState = SHOW_BOOT;
DisplayState previousState = SHOW_BOOT;

// ============================================================================
//                          WIFIMANAGER (sólo bajo demanda)
// ============================================================================
WiFiManager wm;

// ============================================================================
//            PERSISTENCIA MANUAL DE CREDENCIALES WIFI + CÓDIGO
// ============================================================================
// Mismo motivo que en la placa de cobro: la persistencia nativa de
// WiFiManager/WiFi.persistent resultó poco confiable en este combo de
// librerías con el portal no bloqueante — se confirmó con hardware real
// que a veces no sobrevive un apagado/encendido. Guardamos todo a mano en
// nuestro propio namespace de Preferences.
Preferences prefs;
static const char *PREFS_NS = "display";
static const char *PREFS_KEY_SSID = "ssid";
static const char *PREFS_KEY_PASS = "pass";
static const char *PREFS_KEY_CODIGO = "pair_code";

void guardarCredencialesWifi(const String &ssid, const String &pass) {
  if (ssid.length() == 0) {
    logLineFwd("!! guardarCredencialesWifi: SSID vacio, no se guarda nada.");
    return;
  }
  prefs.begin(PREFS_NS, false);
  prefs.putString(PREFS_KEY_SSID, ssid);
  prefs.putString(PREFS_KEY_PASS, pass);
  prefs.end();
}

bool leerCredencialesWifi(String &ssidOut, String &passOut) {
  prefs.begin(PREFS_NS, true);
  ssidOut = prefs.getString(PREFS_KEY_SSID, "");
  passOut = prefs.getString(PREFS_KEY_PASS, "");
  prefs.end();
  return ssidOut.length() > 0;
}

void guardarCodigoEmparejamiento(const String &codigo) {
  prefs.begin(PREFS_NS, false);
  prefs.putString(PREFS_KEY_CODIGO, codigo);
  prefs.end();
}

String leerCodigoEmparejamiento() {
  prefs.begin(PREFS_NS, true);
  String codigo = prefs.getString(PREFS_KEY_CODIGO, "");
  prefs.end();
  return codigo;
}

String pairingCode = ""; // cacheado en RAM luego de leerlo de Preferences

// ============================================================================
//                          UTILIDADES DE LOG
// ============================================================================
void logLineFwd(const String &s) {
  if (DEBUG_LOG) Serial.println(s);
}
// Alias corto para el resto del archivo.
#define logLine logLineFwd

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

// El QR se dibuja centrado en un cuadrado que entra en el ancho de la
// pantalla (320px) aunque la pantalla sea más alta (480px) — deja franjas
// arriba/abajo, que es mejor que un QR chiquito o deformado.
void dibujarQR(const String &data) {
  gfx->fillScreen(COLOR_BLANCO);

  if (data.length() == 0) {
    centrarTexto("Sin datos", SCREEN_HEIGHT / 2, COLOR_NEGRO, 3);
    return;
  }

  int versionUsada = versionMinimaNecesaria(data.length());
  if (versionUsada == 0) {
    logLine("!! QR: dato demasiado largo.");
    centrarTexto("Error QR", SCREEN_HEIGHT / 2, COLOR_NEGRO, 3);
    return;
  }

  uint8_t qrcodeData[qrcode_getBufferSize(versionUsada)];
  QRCode qrcode;
  int8_t resultado = qrcode_initText(&qrcode, qrcodeData, versionUsada, ECC_LOW, data.c_str());

  if (resultado != 0) {
    logLine("!! QR: error generando (codigo " + String(resultado) + ").");
    centrarTexto("Error QR", SCREEN_HEIGHT / 2, COLOR_NEGRO, 3);
    return;
  }

  const int QUIET_ZONE_MODULES = 4;
  int totalModulosConZona = qrcode.size + QUIET_ZONE_MODULES * 2;
  int ladoDisponible = SCREEN_WIDTH; // cuadrado acotado al ancho
  int pixelPorModulo = ladoDisponible / totalModulosConZona;
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

void dibujarPantallaBoot(const String &msg) {
  gfx->fillScreen(COLOR_NEGRO);
  centrarTexto("QRcade", SCREEN_HEIGHT / 2 - 30, COLOR_AZUL, 3);
  centrarTexto(msg, SCREEN_HEIGHT / 2 + 30, COLOR_BLANCO, 2);
  gfx->flush();
}

void dibujarPantallaOffline() {
  gfx->fillScreen(COLOR_ROJO);
  centrarTexto("Fuera de", SCREEN_HEIGHT / 2 - 40, COLOR_BLANCO, 3);
  centrarTexto("Servicio", SCREEN_HEIGHT / 2 + 10, COLOR_BLANCO, 3);
  centrarTexto("QRcade", SCREEN_HEIGHT - 50, COLOR_AZUL, 2);
}

void dibujarPantallaPausada() {
  gfx->fillScreen(COLOR_NEGRO);
  centrarTexto("Maquina en", SCREEN_HEIGHT / 2 - 30, COLOR_GRIS, 3);
  centrarTexto("pausa", SCREEN_HEIGHT / 2 + 20, COLOR_GRIS, 3);
  centrarTexto("QRcade", SCREEN_HEIGHT - 50, COLOR_AZUL, 2);
}

void dibujarPantallaSinCodigo() {
  gfx->fillScreen(COLOR_GRIS);
  centrarTexto("Falta cargar", SCREEN_HEIGHT / 2 - 50, COLOR_NEGRO, 2);
  centrarTexto("codigo de", SCREEN_HEIGHT / 2 - 20, COLOR_NEGRO, 2);
  centrarTexto("emparejamiento", SCREEN_HEIGHT / 2 + 10, COLOR_NEGRO, 2);
  centrarTexto("(boton BOOT 2s)", SCREEN_HEIGHT / 2 + 50, COLOR_NEGRO, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 50, COLOR_AZUL, 2);
}

void dibujarPantallaCodigoInvalido() {
  gfx->fillScreen(COLOR_ROJO);
  centrarTexto("Codigo de", SCREEN_HEIGHT / 2 - 50, COLOR_BLANCO, 2);
  centrarTexto("emparejamiento", SCREEN_HEIGHT / 2 - 20, COLOR_BLANCO, 2);
  centrarTexto("invalido", SCREEN_HEIGHT / 2 + 10, COLOR_BLANCO, 2);
  centrarTexto("(boton BOOT 2s)", SCREEN_HEIGHT / 2 + 50, COLOR_BLANCO, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 50, COLOR_AZUL, 2);
}

void dibujarPantallaConfigPortal() {
  gfx->fillScreen(COLOR_AZUL);
  centrarTexto("Modo config.", SCREEN_HEIGHT / 2 - 50, COLOR_BLANCO, 3);
  centrarTexto("Conectate a:", SCREEN_HEIGHT / 2, COLOR_BLANCO, 2);
  centrarTexto("QRcade_Display", SCREEN_HEIGHT / 2 + 35, COLOR_AMARILLO, 2);
  gfx->flush();
}

void actualizarPantalla() {
  if (currentState == previousState) return;
  previousState = currentState;

  switch (currentState) {
    case SHOW_BOOT: return; // ya se dibujo y mando a pantalla a mano
    case SHOW_QR: dibujarQR(cachedQrData); break;
    case SHOW_OFFLINE: dibujarPantallaOffline(); break;
    case SHOW_PAUSADA: dibujarPantallaPausada(); break;
    case SHOW_SIN_CODIGO: dibujarPantallaSinCodigo(); break;
    case SHOW_CODIGO_INVALIDO: dibujarPantallaCodigoInvalido(); break;
    case SHOW_CONFIG_PORTAL: return; // ya se dibujo y mando a pantalla a mano
  }
  // Con Arduino_Canvas nada se ve en el panel real hasta llamar flush() —
  // es el paso que faltaba al adaptar esto de la pantalla SPI simple de
  // la garra (ahí cada fillRect/print salía directo al panel).
  gfx->flush();
}

// ============================================================================
//          CONSULTA AL BACKEND (socket TLS crudo, nunca HTTPClient)
// ============================================================================
// Mismo patrón que backendCheckin() en la placa de cobro: timeouts duros
// en cada fase para que esta función SIEMPRE vuelva en pocos segundos,
// pase lo que pase del otro lado.
bool consultarQR(const String &codigo, JsonDocument &respuesta, int &httpStatus) {
  const unsigned long CONNECT_TIMEOUT_MS = 5000;
  const unsigned long RESPUESTA_DEADLINE_MS = 6000;

  httpStatus = -1;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(CONNECT_TIMEOUT_MS / 1000);

  if (!client.connect(BACKEND_HOST, BACKEND_PORT, CONNECT_TIMEOUT_MS)) {
    logLine("!! display: no se pudo conectar al backend. RSSI=" + String(WiFi.RSSI()) +
            " heapLibre=" + String(ESP.getFreeHeap()));
    return false;
  }

  String path = "/api/display/" + codigo + "/qr";
  String request = "GET " + path + " HTTP/1.1\r\n";
  request += "Host: " + String(BACKEND_HOST) + "\r\n";
  request += "Connection: close\r\n\r\n";

  client.print(request);

  unsigned long tEsperaInicio = millis();
  while (client.connected() && !client.available()) {
    if (millis() - tEsperaInicio > RESPUESTA_DEADLINE_MS) {
      logLine("!! display: timeout esperando respuesta.");
      client.stop();
      return false;
    }
    delay(10);
    esp_task_wdt_reset();
  }

  String statusLine = client.available() ? client.readStringUntil('\n') : "";
  statusLine.trim();
  // Ej: "HTTP/1.1 200 OK" o "HTTP/1.1 404 Not Found"
  int firstSpace = statusLine.indexOf(' ');
  if (firstSpace > 0) {
    httpStatus = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
  }

  bool chunked = false;
  unsigned long tHeaders0 = millis();
  while (client.connected() || client.available()) {
    if (millis() - tHeaders0 > RESPUESTA_DEADLINE_MS) {
      logLine("!! display: timeout leyendo headers.");
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
  payload.reserve(512);
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
    logLine("---- display " + statusLine);
    logLine(payload);
  }

  if (payload.length() == 0) {
    logLine("!! display: respuesta vacia.");
    return false;
  }

  DeserializationError err = deserializeJson(respuesta, payload);
  if (err) {
    logLine("!! display: no se pudo parsear el JSON de respuesta.");
    return false;
  }
  return true;
}

void hacerPoll() {
  if (pairingCode.length() == 0) {
    pollStatus = "sin_codigo";
    return;
  }

  StaticJsonDocument<768> doc;
  int httpStatus = -1;
  bool ok = consultarQR(pairingCode, doc, httpStatus);

  if (!ok) {
    pollFailCount++;
    // No cambiamos pollStatus en una falla de red transitoria — se
    // mantiene el último QR bueno en pantalla hasta que vuelva a andar
    // (ver resolverEstadoVisible: sólo pasa a OFFLINE por falta de WiFi,
    // no por un solo poll fallido).
    return;
  }
  pollOkCount++;

  if (httpStatus == 404) {
    pollStatus = "no_encontrado";
    return;
  }
  if (httpStatus != 200) {
    logLine("!! display: status HTTP inesperado: " + String(httpStatus));
    return;
  }

  pollStatus = "ok";
  devicePaused = doc["is_paused"] | false;
  const char *qr = doc["qr_data"] | (const char *)nullptr;
  cachedQrData = (qr != nullptr) ? String(qr) : "";
}

void resolverEstadoVisible() {
  if (WiFi.status() != WL_CONNECTED) {
    currentState = SHOW_OFFLINE;
    return;
  }
  if (pollStatus == "sin_codigo") {
    currentState = SHOW_SIN_CODIGO;
    return;
  }
  if (pollStatus == "no_encontrado") {
    currentState = SHOW_CODIGO_INVALIDO;
    return;
  }
  if (pollStatus != "ok") {
    // Todavia sin la primera respuesta exitosa del backend.
    return;
  }
  if (devicePaused) {
    currentState = SHOW_PAUSADA;
    return;
  }
  currentState = SHOW_QR;
}

// ============================================================================
//                    SALUD DEL SISTEMA (heap / uptime / wifi)
// ============================================================================
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
//    PORTAL DE CONFIGURACION (WiFi + campo extra de codigo de emparejamiento)
// ============================================================================
// Mismo patrón bloqueante-controlado que la placa de cobro: mientras el
// portal está abierto no hay ningún otro tráfico de red, así que nunca
// coexisten dos radios/TLS a la vez.
void abrirPortalDeConfiguracion() {
  currentState = SHOW_CONFIG_PORTAL;
  previousState = SHOW_BOOT;
  dibujarPantallaConfigPortal();

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(100);

  // Campo extra del portal para el código de 8 caracteres (ver
  // DisplayPairingCard.tsx en el panel). Se precarga con el valor ya
  // guardado, si había uno, para que regenerar sólo el WiFi no obligue a
  // retipear el código.
  String codigoPrevio = leerCodigoEmparejamiento();
  char codigoPrevioBuf[17];
  codigoPrevio.toCharArray(codigoPrevioBuf, sizeof(codigoPrevioBuf));

  WiFiManagerParameter paramCodigo(
    "pair_code",
    "Codigo de emparejamiento (panel QRcade)",
    codigoPrevioBuf,
    16
  );
  wm.addParameter(&paramCodigo);

  configurarWatchdog(WDT_TIMEOUT_PORTAL_S);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0); // el timeout real lo controlamos abajo
  wm.startConfigPortal("QRcade_Display");
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

    if (WiFi.status() == WL_CONNECTED) break;
    delay(10);
  }

  logLine(">> Portal de configuracion: WiFi configurado con exito.");
  guardarCredencialesWifi(wm.getWiFiSSID(), wm.getWiFiPass());

  // El valor del campo extra sólo es confiable DESPUES de que el portal
  // cerró con éxito (getValue() lee el último submit del formulario).
  String codigoNuevo = String(paramCodigo.getValue());
  codigoNuevo.trim();
  codigoNuevo.toUpperCase();
  if (codigoNuevo.length() > 0) {
    guardarCodigoEmparejamiento(codigoNuevo);
    pairingCode = codigoNuevo;
    logLine(">> Codigo de emparejamiento guardado: " + codigoNuevo);
  } else {
    logLine("!! Portal cerrado sin codigo de emparejamiento cargado.");
  }
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

  // Pantalla primero, incluido el backlight — si algo de ahí falla
  // queremos verlo lo antes posible, y así evitamos mostrar basura sin
  // luz de fondo durante el resto del boot.
  gfx->begin();
  gfx->fillScreen(COLOR_NEGRO);
  gfx->flush();
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH); // backlight de esta placa es activo en HIGH

  dibujarPantallaBoot("Iniciando...");

  WiFi.mode(WIFI_STA);
  delay(100);

  logLine("");
  logLine("========================================");
  logLine("QRcade firmware (display) v" + String(FIRMWARE_VERSION));
  logLine("MAC: " + WiFi.macAddress());
  logLine("========================================");

  bool pedirPortal = botonConfigMantenidoAlBoot();
  if (pedirPortal) {
    logLine(">> Boton de configuracion detectado al arrancar.");
  }

  esp_task_wdt_reset();

  String ssidGuardado, passGuardado;
  bool hayCredenciales = leerCredencialesWifi(ssidGuardado, passGuardado);
  if (hayCredenciales) {
    logLine(">> Credenciales guardadas encontradas para SSID: " + ssidGuardado);
  } else {
    logLine(">> No hay credenciales guardadas en Preferences.");
  }

  if (pedirPortal || !hayCredenciales) {
    abrirPortalDeConfiguracion();
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

  pairingCode = leerCodigoEmparejamiento();
  if (pairingCode.length() == 0) {
    logLine("!! Sin codigo de emparejamiento guardado todavia.");
  } else {
    logLine(">> Codigo de emparejamiento: " + pairingCode);
  }

  bootAt = millis();
  lastWifiOkAt = millis();

  dibujarPantallaBoot("Sincronizando...");
  hacerPoll();
  resolverEstadoVisible();
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
    }
    static unsigned long lastReconnectTry = 0;
    if (millis() - lastReconnectTry > 10000) {
      lastReconnectTry = millis();
      logLine("!! WiFi desconectado, reintentando...");
      WiFi.reconnect();
    }
  } else {
    if (millis() - lastPollAt >= POLL_INTERVAL_MS) {
      lastPollAt = millis();
      hacerPoll();
      resolverEstadoVisible();
    }
  }

  // Reabrir el portal manualmente en operación normal (sin reiniciar),
  // por si hay que recargar el código sin desenchufar la placa.
  static bool botonChequeado = false;
  static unsigned long botonDesde = 0;
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    if (!botonChequeado) {
      botonChequeado = true;
      botonDesde = millis();
    } else if (millis() - botonDesde >= CONFIG_BUTTON_HOLD_MS) {
      botonChequeado = false;
      abrirPortalDeConfiguracion();
      previousState = SHOW_BOOT;
      lastPollAt = 0; // fuerza un poll inmediato al volver
    }
  } else {
    botonChequeado = false;
  }

  actualizarPantalla();
  chequearSaludYReiniciarSiHaceFalta();

  if (Serial.available() > 0) {
    char key = Serial.read();
    if (key == 'h' || key == 'H') {
      Serial.println("Heap libre: " + String(ESP.getFreeHeap()) + " bytes");
      Serial.println("Polls OK: " + String(pollOkCount) + "  fallidos: " + String(pollFailCount));
      Serial.println("Uptime: " + String((millis() - bootAt) / 1000) + "s");
      Serial.println("Codigo: " + pairingCode);
    }
  }

  delay(10);
}
