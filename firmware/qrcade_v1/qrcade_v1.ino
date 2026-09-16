/*
  QRcade — Firmware v1.0 (arquitectura con backend propio)
  ---------------------------------------------------------------------
  Reemplaza al firmware anterior (qrcade_DISPLAY_OP1.ino), que hablaba
  directo con la API de Mercado Pago desde la ESP. Acá la ESP ya NO
  llama a Mercado Pago en ningún momento: sólo hace check-in periódico
  contra NUESTRO backend (el panel QRcade), que es quien habla con MP,
  procesa los pagos vía webhook y le dice a la placa "tirá una ficha".

  Qué cambia respecto al firmware anterior:
    - Se borran mpGet/mpPost/mpPut, obtenerUserId, buscarOCrearTienda,
      buscarOCrearCaja, bloquearPrecioFijo, obtenerUltimoPagoAprobado,
      confirmarCajaExiste, provisionarMercadoPago, checkMercadoPago:
      TODO eso ahora vive en el backend (Next.js), no en la placa. Esto
      elimina de raíz la clase de bug de HTTPClient/WiFiClientSecure
      que se colgaba pegándole directo a la API de MP.
    - El portal cautivo (WiFiManager) vuelve a ser SOLO WiFi — Access
      Token, Local, Caja y Monto ahora se configuran desde el panel web
      del dueño, no desde la placa.
    - En cada check-in la placa manda {mac, factory_key, firmware_version,
      wifi_rssi} a POST /api/device/checkin y recibe: estado de la placa
      (unclaimed / claimed / revoked), el QR de cobro (cuando el backend
      ya lo tenga armado — ver nota más abajo), y una cola de comandos
      pendientes (restart, test_dispense, y a futuro dispense con
      cantidad) para ejecutar en el momento.
    - El código de 6 caracteres para vincular la placa NO se muestra acá:
      lo ve el admin en su panel y se lo pasa al dueño. La pantalla de
      "placa sin reclamar" sólo informa que está esperando activación.
    - Dibujo de QR (qrcode_impl.c/h, ricmoo/Nayuki) y la máquina de
      estados de pantalla se mantienen igual que antes.

  NOTA sobre el QR de cobro: el rediseño de integración con Mercado
  Pago (token por cuenta del dueño, modo "monto fijo" vs "fichas por
  combo") todavía no está implementado en el backend — hoy
  /api/device/checkin siempre devuelve qr_data: null para una placa
  reclamada. Este firmware ya contempla ese campo: en cuanto el
  backend lo empiece a mandar, la placa lo dibuja sin necesidad de
  ningún cambio de firmware. Mientras tanto muestra "Cobro sin
  configurar".

  Librerías necesarias (Arduino Library Manager):
    - WiFiManager (tzapu)
    - ArduinoJson
    - GFX Library for Arduino (moononournation / Arduino_GFX_Library)
    - QRCode (ricmoo) — se usa la copia local qrcode_impl.c/h
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include "qrcode_impl.h"

// ==================== CONFIG DE FÁBRICA (por placa) ====================
// Generada una vez con `openssl rand -hex 24` (ver README del panel web).
// Es la misma para toda la tanda de placas que arranca con este .ino; el
// hash correspondiente vive en app_settings.factory_key_hash en Supabase.
const char *FACTORY_KEY = "REEMPLAZAR_CON_LA_CLAVE_DE_FABRICA";

// Host del backend (panel QRcade). Sin "https://" ni barra final.
const char *BACKEND_HOST = "REEMPLAZAR_CON_TU_DOMINIO.vercel.app";
const uint16_t BACKEND_PORT = 443;
const char *CHECKIN_PATH = "/api/device/checkin";

const char *FIRMWARE_VERSION = "1.0.0";

// ==================== CONFIG DE HARDWARE ====================
const int RELAY_PIN = 25;
const unsigned long PULSE_DURATION = 500;

#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_MISO  -1
#define TFT_RST   4
#define TFT_DC    19
#define TFT_CS    -1

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0, true, SCREEN_WIDTH, SCREEN_HEIGHT);

#define COLOR_NEGRO   0x0000
#define COLOR_BLANCO  0xFFFF
#define COLOR_VERDE   0x07E0
#define COLOR_ROJO    0xF800
#define COLOR_AZUL    0x051D
#define COLOR_GRIS    0x7BEF

// ==================== DEBUG ====================
const bool DEBUG_BACKEND = true;

// ==================== ESTADO DEL CHECK-IN ====================
Preferences preferences;

unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 5000;

String deviceStatus = "";        // "unclaimed" | "claimed" | "revoked" | "" (aún sin respuesta)
bool devicePaused = false;
String cachedQrData = "";

// ==================== ESTADO DE PANTALLA ====================
enum DisplayState {
  SHOW_BOOT,
  SHOW_QR,
  SHOW_IDLE,
  SHOW_PAID,
  SHOW_OFFLINE,
  SHOW_UNCLAIMED,
  SHOW_UNCONFIGURED,
  SHOW_PAUSED,
  SHOW_REVOKED
};
DisplayState currentState = SHOW_BOOT;
DisplayState previousState = SHOW_BOOT;
unsigned long displayStateStart = 0;

const unsigned long QR_DURATION = 6000;
const unsigned long IDLE_DURATION = 2000;
const unsigned long PAID_DURATION = 4000;

// ==================== VENTANA DE RECONFIGURACION (WiFi cautiva) ====================
// A diferencia del firmware anterior, el portal ahora es SOLO WiFi: el
// Access Token / Local / Caja / Monto se cargan desde el panel web del
// dueño, no desde la placa.
WiFiManager wm;

bool portalWindowActive = false;
unsigned long portalWindowStart = 0;
const unsigned long PORTAL_WINDOW_MS = 30000;

// =====================================================================
//                          RELE
// =====================================================================
void triggerCoinPulse() {
  Serial.println(">> Disparando rele...");
  digitalWrite(RELAY_PIN, LOW);
  delay(PULSE_DURATION);
  digitalWrite(RELAY_PIN, HIGH);
  Serial.println(">> Pulso completado.");
}

void triggerCoinPulses(int veces) {
  if (veces < 1) veces = 1;
  for (int i = 0; i < veces; i++) {
    triggerCoinPulse();
    if (i < veces - 1) delay(250);
  }
}

// =====================================================================
//                    CHECK-IN CONTRA NUESTRO BACKEND
// =====================================================================
// Usa un socket TLS crudo con timeouts explícitos (en vez de HTTPClient)
// para nunca quedar colgada esperando: esto es lo que causaba el bug de
// cuelgue del firmware anterior cuando le pegaba directo a la API de MP.
// Acá le pegamos a nuestro propio backend, pero mantenemos el mismo
// patrón defensivo por las dudas (caída de red, backend lento, etc).
bool backendCheckin(JsonDocument &respuesta) {
  const unsigned long CONNECT_TIMEOUT_MS = 5000;
  const unsigned long RESPUESTA_DEADLINE_MS = 6000;

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(BACKEND_HOST, BACKEND_PORT, CONNECT_TIMEOUT_MS)) {
    Serial.println("!! checkin: no se pudo conectar al backend.");
    return false;
  }

  String mac = WiFi.macAddress();

  DynamicJsonDocument bodyDoc(256);
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
      Serial.println("!! checkin: timeout esperando respuesta del backend.");
      client.stop();
      return false;
    }
    delay(10);
  }

  // Salteamos los headers HTTP hasta la línea en blanco.
  String statusLine = client.available() ? client.readStringUntil('\n') : "";
  statusLine.trim();

  bool contentLengthChunked = false;
  while (client.connected() || client.available()) {
    String linea = client.readStringUntil('\n');
    linea.trim();
    if (linea.indexOf("Transfer-Encoding: chunked") >= 0) contentLengthChunked = true;
    if (linea.length() == 0) break;
  }

  String payload = "";
  unsigned long tLecturaInicio = millis();
  while ((client.connected() || client.available()) &&
         millis() - tLecturaInicio < RESPUESTA_DEADLINE_MS) {
    while (client.available()) {
      payload += (char)client.read();
    }
    if (!client.connected() && !client.available()) break;
  }
  client.stop();

  if (contentLengthChunked) {
    // Los chunks vienen precedidos por su tamaño en hex + \r\n; para un
    // JSON chico como el nuestro alcanza con quedarnos con lo que haya
    // entre el primer y el último caracter de llave.
    int inicio = payload.indexOf('{');
    int fin = payload.lastIndexOf('}');
    if (inicio >= 0 && fin > inicio) {
      payload = payload.substring(inicio, fin + 1);
    }
  }

  if (DEBUG_BACKEND) {
    Serial.println("---- checkin " + statusLine);
    Serial.println(payload);
  }

  if (payload.length() == 0) {
    Serial.println("!! checkin: respuesta vacía.");
    return false;
  }

  DeserializationError err = deserializeJson(respuesta, payload);
  if (err) {
    Serial.println("!! checkin: no se pudo parsear el JSON de respuesta.");
    return false;
  }
  return true;
}

void procesarComandos(JsonArray comandos) {
  for (JsonVariant c : comandos) {
    String comando = c.as<String>();
    Serial.println(">> Comando recibido: " + comando);

    if (comando == "restart") {
      Serial.println(">> Reiniciando por orden del panel...");
      delay(200);
      ESP.restart();
    } else if (comando == "test_dispense") {
      triggerCoinPulses(1);
      currentState = SHOW_PAID;
      displayStateStart = millis();
    } else if (comando.startsWith("dispense:")) {
      // Formato futuro para el modo de fichas por combo: "dispense:N".
      int veces = comando.substring(9).toInt();
      triggerCoinPulses(veces > 0 ? veces : 1);
      currentState = SHOW_PAID;
      displayStateStart = millis();
    } else {
      Serial.println("!! Comando desconocido, se ignora: " + comando);
    }
  }
}

void hacerCheckin() {
  DynamicJsonDocument doc(1024);
  if (!backendCheckin(doc)) {
    return;
  }

  String status = doc["status"] | "";
  deviceStatus = status;

  if (status == "revoked") {
    return;
  }

  if (status == "unclaimed") {
    return;
  }

  if (status == "claimed") {
    devicePaused = doc["is_paused"] | false;
    const char *qr = doc["qr_data"] | (const char *)nullptr;
    cachedQrData = (qr != nullptr) ? String(qr) : "";

    if (doc["commands"].is<JsonArray>()) {
      procesarComandos(doc["commands"].as<JsonArray>());
    }
  }
}

// =====================================================================
//                             PANTALLA
// =====================================================================
void centrarTexto(String texto, int y, uint16_t color, uint8_t size) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(texto, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
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
    if (QR_BYTE_CAPACITY_ECC_LOW[v - 1] >= largoDato) {
      return v;
    }
  }
  return 0;
}

void dibujarQR(String data) {
  gfx->fillScreen(COLOR_BLANCO);

  Serial.println("Generando QR. Largo del dato: " + String(data.length()));

  int versionUsada = versionMinimaNecesaria(data.length());
  if (versionUsada == 0) {
    Serial.println("!! Error: el dato es demasiado largo, no entra ni en version " + String(QR_VERSION_MAX));
    centrarTexto("Error QR", SCREEN_HEIGHT / 2, COLOR_NEGRO, 2);
    return;
  }

  uint8_t qrcodeData[qrcode_getBufferSize(versionUsada)];
  QRCode qrcode;
  int8_t resultado = qrcode_initText(&qrcode, qrcodeData, versionUsada, ECC_LOW, data.c_str());

  if (resultado != 0) {
    Serial.println("!! Error generando el QR (codigo " + String(resultado) + ") con version " + String(versionUsada));
    centrarTexto("Error QR", SCREEN_HEIGHT / 2, COLOR_NEGRO, 2);
    return;
  }

  Serial.println("QR generado OK. Version usada: " + String(versionUsada) +
                  "  Tamano (modulos): " + String(qrcode.size));

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

void dibujarPantallaBoot(String msg) {
  gfx->fillScreen(COLOR_NEGRO);
  centrarTexto("QRcade", SCREEN_HEIGHT / 2 - 20, COLOR_AZUL, 2);
  centrarTexto(msg, SCREEN_HEIGHT / 2 + 20, COLOR_BLANCO, 1);
}

void dibujarPantallaSinReclamar() {
  gfx->fillScreen(COLOR_NEGRO);
  centrarTexto("Esperando", SCREEN_HEIGHT / 2 - 20, COLOR_BLANCO, 2);
  centrarTexto("activacion...", SCREEN_HEIGHT / 2 + 10, COLOR_BLANCO, 2);
  centrarTexto("QRcade", SCREEN_HEIGHT - 34, COLOR_AZUL, 2);
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

void actualizarPantalla() {
  if (currentState == previousState) return;

  switch (currentState) {
    case SHOW_QR:
      dibujarQR(cachedQrData);
      break;
    case SHOW_IDLE:
      dibujarPantallaEspera();
      break;
    case SHOW_PAID:
      dibujarPantallaPagoOk();
      break;
    case SHOW_OFFLINE:
      dibujarPantallaOffline();
      break;
    case SHOW_UNCLAIMED:
      dibujarPantallaSinReclamar();
      break;
    case SHOW_UNCONFIGURED:
      dibujarPantallaSinConfigurar();
      break;
    case SHOW_PAUSED:
      dibujarPantallaPausada();
      break;
    case SHOW_REVOKED:
      dibujarPantallaRevocada();
      break;
    case SHOW_BOOT:
      dibujarPantallaBoot("Iniciando...");
      break;
  }
  previousState = currentState;
}

// Decide qué pantalla corresponde según el último check-in conocido.
// No se llama mientras currentState == SHOW_PAID (esa la maneja el timer
// de abajo para no cortar el cartel de "Pago Exitoso").
void resolverEstadoSegunBackend() {
  if (currentState == SHOW_PAID) return;

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
      // Deja que el ciclo QR <-> IDLE siga su curso; sólo forzamos QR si
      // veníamos de otro estado distinto.
      objetivo = (currentState == SHOW_QR || currentState == SHOW_IDLE)
                   ? currentState
                   : SHOW_QR;
    }
  } else {
    // Todavía sin respuesta del backend en este boot.
    objetivo = SHOW_BOOT;
  }

  if (objetivo != currentState) {
    currentState = objetivo;
    displayStateStart = millis();
  }
}

// =====================================================================
//                              SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(50);
  digitalWrite(TFT_RST, LOW);  delay(100);
  digitalWrite(TFT_RST, HIGH); delay(100);

  gfx->begin();
  dibujarPantallaBoot("Conectando WiFi...");

  wm.setConfigPortalTimeout(180);
  bool conectado = wm.autoConnect("QRcade_Config");

  if (!conectado) {
    Serial.println("No se pudo conectar. Reiniciando...");
    ESP.restart();
  }

  Serial.println("Conectado con exito a Internet!");
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  dibujarPantallaBoot("Sincronizando...");
  hacerCheckin();
  resolverEstadoSegunBackend();
  previousState = SHOW_BOOT;

  // Ventana de 30s post-boot donde la red "QRcade_Config" queda
  // disponible para recargar el WiFi sin tener que reiniciar la placa.
  wm.setConfigPortalTimeout(0);
  wm.setConfigPortalBlocking(false);
  wm.startConfigPortal("QRcade_Config");
  portalWindowActive = true;
  portalWindowStart = millis();
  Serial.println(">> Ventana de reconfiguracion abierta por 30s (red QRcade_Config).");
}

// =====================================================================
//                               LOOP
// =====================================================================
void loop() {
  if (portalWindowActive) {
    wm.process();

    if (WiFi.softAPgetStationNum() > 0) {
      portalWindowStart = millis();
    } else if (millis() - portalWindowStart >= PORTAL_WINDOW_MS) {
      Serial.println(">> Ventana de reconfiguracion cerrada (30s sin uso).");
      wm.stopConfigPortal();
      portalWindowActive = false;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (currentState != SHOW_OFFLINE) {
      currentState = SHOW_OFFLINE;
      displayStateStart = millis();
    }
  } else {
    if (currentState == SHOW_OFFLINE) {
      currentState = SHOW_BOOT;
      displayStateStart = millis();
    }

    if (millis() - lastCheckTime >= checkInterval) {
      lastCheckTime = millis();
      hacerCheckin();
      resolverEstadoSegunBackend();
    }

    unsigned long elapsed = millis() - displayStateStart;
    if (currentState == SHOW_QR && elapsed >= QR_DURATION) {
      currentState = SHOW_IDLE;
      displayStateStart = millis();
    } else if (currentState == SHOW_IDLE && elapsed >= IDLE_DURATION) {
      currentState = SHOW_QR;
      displayStateStart = millis();
    } else if (currentState == SHOW_PAID && elapsed >= PAID_DURATION) {
      currentState = (cachedQrData != "") ? SHOW_QR : SHOW_IDLE;
      displayStateStart = millis();
    }
  }

  actualizarPantalla();

  if (Serial.available() > 0) {
    char key = Serial.read();
    if (key == 'c' || key == 'C') {
      triggerCoinPulse();
    }
  }
}
