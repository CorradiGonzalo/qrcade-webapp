/*
  ============================================================================
  NexoQR — Firmware "display" v2.5.1 (pantalla externa para cafeteras)
  Placa: Guition JC3248W535 — ESP32-S3, pantalla IPS 3.5" 320x480,
  controlador AXS15231B por bus QSPI.
  ============================================================================
  FIX v2.5.1 (la pantalla nunca bajaba el logo subido desde la app):
  El bug era 100% del lado del BACKEND, no de esta placa: api/display/
  [code]/qr mandaba la "version" del logo como epoch en MILISEGUNDOS
  (13 dígitos, ~1.7 billones) — mucho más grande de lo que entra en un
  "long" de 32 bits (max ~2147 millones), que es lo que usa esta placa
  para comparar. Resultado: la version se leía truncada/en 0 y esta
  pantalla nunca detectaba el cambio, así que nunca descargaba nada.
  El fix fue cambiar el backend para que mande la version en SEGUNDOS
  en vez de milisegundos (entra cómodo en 32 bits, hasta 2038) — esta
  placa NO CAMBIÓ SU LÓGICA, sigue comparando con un "long" exactamente
  igual que en v2.5.0. Por eso una placa que YA tiene el v2.5.0 flasheado
  arranca a funcionar sola, sin reflashear nada, en cuanto el backend
  se redeploye — este v2.5.1 es sólo para los comentarios/versionado,
  el código es funcionalmente idéntico al v2.5.0 en esta parte.

  NOVEDAD v2.5.0 (logos subidos desde la app, sin tocar la microSD a mano):
  Hasta ahora LOGO1.BIN ("logo del cliente") y LOGO2.BIN ("logo NexoQR")
  sólo se podían cambiar sacando la microSD y copiando el archivo a mano.
  Ahora el dueño puede subir el logo del cliente desde /dashboard (por
  máquina) y un admin puede subir el logo NexoQR desde /admin/firmware
  (uno solo, global para todas las pantallas) — ver migración 0019 y
  api/display/[code]/qr. Esta pantalla sigue pidiendo ese mismo endpoint
  cada ~15s como siempre (hacerPoll/consultarQR, SIN CAMBIOS); lo único
  nuevo es que ahora el JSON puede traer 4 campos más
  (logo_cliente_url/version, logo_nexo_url/version) y, si alguno trae una
  versión distinta de la que ya tiene guardada, esta pantalla descarga
  ese .BIN por HTTPS y lo guarda en la SD en el lugar de siempre
  (verificarYActualizarLogos/descargarLogoASD, funciones NUEVAS, ver más
  abajo). Si el dueño/admin nunca subió nada por la app, esos campos
  vienen null y esta pantalla no toca absolutamente nada de lo que ya
  tenga en la SD — sigue funcionando exactamente igual que antes. Cero
  cambios en el slideshow, el QR, el teclado de PIN o el aviso de pago:
  sólo se agregó código nuevo, nada existente se modificó.

  NOVEDAD v2.4.0 (ocultar el QR mientras la cafetera está limpiando):
  La placa de cobro (qrcade_cafetera_v1.1.0) ahora manda si está en medio
  de un ciclo de limpieza (ver migración 0017, campo devices.is_cleaning,
  que llega en cada poll de esta pantalla vía api/display/[code]/qr). Si
  es así, esta pantalla deja de mostrar el QR y la botonera de pago queda
  inhabilitada del lado de la cafetera durante ese rato — mostrar el QR
  igual permitiría que alguien pague y nunca reciba su café. Prioridad
  igual a "máquina pausada": ni el QR normal ni el QR oferta se ven
  mientras dura. Apenas is_cleaning vuelve a false, la pantalla retoma el
  ciclo normal sola, desde el logo (como si recién hubiera vuelto de
  offline).

  NOVEDAD v2.3.0 (aviso de "Pago acreditado"):
  Cuando se acredita un pago (del QR principal o del QR oferta, no
  distingue cuál), la pantalla lo detecta en el siguiente poll (hasta
  ~15s de demora, el mismo intervalo de siempre) y muestra un cartel de
  "Pago acreditado — Disfruta tu café" con un dibujo de taza, durante 6
  segundos, antes de volver sola al ciclo normal. La detección es por
  cambio de devices.last_payment_at (ver migración 0016) — el webhook de
  Mercado Pago lo actualiza cada vez que efectivamente tira una ficha,
  así que un pago que no matchea ningún combo (y por lo tanto no tira
  nada) tampoco dispara este aviso.

  NOVEDAD v2.2.0 ("QR de empleado"):
  A pedido del cliente, además del QR normal (para clientes — SIN NINGÚN
  CAMBIO, exactamente el mismo de siempre) esta placa puede mostrar un
  SEGUNDO QR, de uso interno, con un precio más bajo. Se accede tocando
  en cualquier lugar de la pantalla mientras se está mostrando el QR
  normal: aparece un teclado numérico para ingresar un PIN (configurado
  desde el panel, en la página de cobro de la máquina — ver migración
  0015 y saveDeviceEmployeeBillingAction). Si el PIN es correcto, se
  muestra el QR de empleado durante 30 segundos y después vuelve sola al
  ciclo normal (logos + QR de cliente). 3 PINs mal puestos seguidos
  bloquean el teclado por 30 segundos. Si no hay PIN configurado para
  esta máquina, tocar la pantalla no hace nada — el QR de cliente nunca
  se ve afectado.

  Requiere el touch de esta placa, que vive en un bus I2C PROPIO (no es
  parte del bus QSPI de la pantalla ni del SPI de la SD): SDA=GPIO4,
  SCL=GPIO8, controlador AXS15231B en la dirección 0x3B, protocolo
  confirmado contra el componente "axs15231" de ESPHome (ver
  leerToque()). Si en tu unidad particular el touch no responde o
  registra en el lugar equivocado, revisar/ajustar TOUCH_SDA/TOUCH_SCL o
  los flags TOUCH_SWAP_XY/TOUCH_INVERT_X/TOUCH_INVERT_Y más abajo —
  nunca hace falta tocar nada de la lógica de cobro para eso.

  NOVEDAD v2.1.1 (fix: la SD y la pantalla competian por el mismo bus SPI):
  La SD se habia puesto en el mismo periferico SPI que usa por defecto el
  bus QSPI de la pantalla (FSPI/SPI2_HOST) -> esto corrompia los colores
  de la pantalla apenas se leia la SD, y podia dejar el refresco de
  pantalla trabado. Se movio la SD al otro periferico (HSPI/SPI3_HOST).
  También se agregó un chequeo en pantalla, al bootear, de que LOGO1.BIN /
  LOGO2.BIN / ICONO.BIN existan en la SD con el tamaño correcto.

  NOVEDAD v2.1.0 (rebranding QR&CA -> NexoQR + slideshow de logos por SD):
  Esta placa muestra ahora un ciclo de 4 pasos en loop mientras está en
  funcionamiento normal (con WiFi y código de emparejamiento OK):
    1) Logo del cliente (ANDREASSI)     — 2 s
    2) Pantalla de cobro con el QR      — 10 s
    3) Logo propio (NEXOQR)             — 2 s
    4) Pantalla de cobro con el QR      — 10 s
    ... y vuelve a 1).
  Los logos se leen de una microSD (slot propio de esta placa, bus SPI
  aparte del QSPI de la pantalla) como archivos .BIN de 320x480 en RGB565
  crudo (LOGO1.BIN = Andreassi, LOGO2.BIN = NexoQR) — no van compilados en
  el firmware, así que para cambiar un logo alcanza con reemplazar el
  archivo en la tarjeta, sin reflashear. Si no hay SD o faltan los
  archivos, el ciclo se salta esos pasos y se comporta como antes (sólo
  pantalla de cobro) — nunca bloquea el funcionamiento del QR.

  NOVEDAD v2.0.0 (rebranding QRcade -> QR&CA + pantalla con marca):
  La v1.0.0 sólo mostraba el QR a pantalla completa. Esa versión agregó,
  sin tocar nada de la lógica de red/pairing:
    - Header con el isotipo + nombre + bajada.
    - El QR, más chico, centrado.
    - Footer con el monto a pagar (si la máquina está en modo "fijo") y un
      mini paso a paso (Escaneá -> Pagá -> Retirá) con íconos dibujados a
      mano (nada de fuentes/emoji: la librería gráfica no los tiene).

  Placa COMPAÑERA de una cafetera ya armada con su propia placa de cobro
  (qrcade_cafetera_v1.0.0.ino — esa es la que tiene el relé, procesa pagos
  de Mercado Pago y NO SE TOCA). Esta placa es sólo una pantalla:

    - NO tiene relé ni ningún pin de salida de potencia.
    - NO habla con Mercado Pago.
    - NO hace /api/device/checkin, no tiene factory_key, no se "reclama"
      desde el panel como una máquina nueva.
    - Lo ÚNICO que hace es pedir, cada pocos segundos, el QR vigente de
      la cafetera a la que está asociada (vía GET /api/display/{codigo}/qr)
      y dibujarlo en pantalla (intercalado con los logos). El aviso de
      pago le llega a la OTRA placa (la que cobra); para el panel, sigue
      siendo una sola máquina.

  El "código de emparejamiento" (8 caracteres, generado desde el panel en
  la página de la máquina — ver DisplayPairingCard.tsx / migración 0014)
  es lo único que vincula esta pantalla con la cafetera correspondiente.

  ----------------------------------------------------------------------------
  Pines pantalla (QSPI, igual que v1.0.0):
    CS=45  SCK=47  D0=21  D1=48  D2=40  D3=39   Backlight=1 (activo HIGH)

  Pines microSD (bus SPI propio, NO el QSPI de la pantalla — pinout del
  slot TF de esta placa según el foro de soporte del fabricante):
    CS=10  SCK=12  MOSI=11  MISO=13
  (si en tu unidad particular no coinciden, se puede ajustar sin tocar el
  resto del firmware — son sólo estos 4 defines).

  Librerías necesarias (Arduino Library Manager):
    - WiFiManager (tzapu)
    - ArduinoJson
    - GFX Library for Arduino (moononournation / Arduino_GFX_Library) 1.5.0+
    - QRCode (ricmoo) — se usa la copia local qrcode_impl.c/h.
    - SD (incluida en el core de Arduino-ESP32, no hace falta instalarla).

  Configuración en Arduino IDE (Tools): igual que v1.0.0 — Board "ESP32S3
  Dev Module", USB CDC On Boot "Enabled", PSRAM "OPI PSRAM", Flash Size
  16MB, cualquier Partition Scheme de 16MB.
  ============================================================================
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>
#include <HTTPClient.h> // sólo para descargarLogoASD() — el resto de las llamadas al backend sigue usando el socket TLS crudo de consultarQR(), sin tocarlo
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <esp_task_wdt.h>
#include "qrcode_impl.h"

// ============================================================================
//                          CONFIG DE BACKEND
// ============================================================================
static const char *BACKEND_HOST = "qrcade.dev";
static const uint16_t BACKEND_PORT = 443;
// Se completa en tiempo de ejecución con "/api/display/" + codigo + "/qr".

static const char *FIRMWARE_VERSION = "display-2.5.1";

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

static const int CONFIG_BUTTON_PIN = 0;
static const unsigned long CONFIG_BUTTON_HOLD_MS = 2000;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED /* reset */, 0 /* rotation */, false /* ips */, SCREEN_WIDTH, SCREEN_HEIGHT);
Arduino_Canvas *gfx = new Arduino_Canvas(SCREEN_WIDTH, SCREEN_HEIGHT, panel, 0, 0, 0);

// ============================================================================
//            TARJETA microSD (logos del slideshow) — bus SPI propio
// ============================================================================
// OJO: en ESP32-S3 el bus QSPI de la pantalla (Arduino_ESP32QSPI, mas
// arriba) usa por defecto el periferico SPI2_HOST, que es justo el mismo
// que "FSPI". Si la SD tambien se abre en FSPI, los dos bus pelean por el
// mismo periferico fisico -- en la practica esto se vio como pantallas con
// un tinte verde raro justo despues de leer la SD, y la pantalla del QR
// dejando de refrescarse (el firmware seguia corriendo bien por dentro,
// pero el bus de la pantalla quedaba en mal estado). Por eso la SD va en
// HSPI (el otro periferico, SPI3_HOST) con sus pines seteados a mano --
// HSPI no tiene pines por defecto en el S3, así que hay que pasarlos sí o sí.
#define SD_CS   10
#define SD_SCK  12
#define SD_MOSI 11
#define SD_MISO 13

SPIClass sdSPI(HSPI);
bool sdDisponible = false;

static const char *RUTA_LOGO_CLIENTE = "/LOGO1.BIN"; // Andreassi, 320x480
static const char *RUTA_LOGO_PROPIO  = "/LOGO2.BIN"; // NexoQR, 320x480
static const char *RUTA_ICONO        = "/ICONO.BIN"; // isotipo NexoQR recortado, 64x64
static const int ICONO_SIZE = 64;

// ============================================================================
//         TOUCH (AXS15231B) — bus I2C propio, para el "QR de empleado"
// ============================================================================
// Bus I2C SEPARADO tanto del QSPI de la pantalla como del SPI de la SD —
// según la doc del controlador táctil (mismo chip que la pantalla, pero el
// touch vive en I2C) y confirmado contra el componente "axs15231" de
// ESPHome: SDA=GPIO4, SCL=GPIO8, dirección 0x3B. Si en hardware real no
// responde, revisar esos 2 pines primero (es lo único que podría variar
// entre unidades).
#define TOUCH_SDA  4
#define TOUCH_SCL  8
#define TOUCH_I2C_ADDR 0x3B

// Si al tocar la pantalla el teclado numérico registra el toque en un
// lugar distinto de donde se tocó, ajustar estos 3 flags durante el
// bring-up (no hace falta tocar nada más del firmware ni de la lógica
// de cobro). Con rotation=0 en el panel (ver `panel` más arriba) lo
// esperable es que no haga falta tocar ninguno, pero el touch de este
// chip puede venir montado con otro mapeo según la unidad.
static const bool TOUCH_SWAP_XY   = false;
static const bool TOUCH_INVERT_X  = false;
static const bool TOUCH_INVERT_Y  = false;

// Comando de lectura del controlador táctil AXS15231B — protocolo
// confirmado contra axs15231_touchscreen.cpp (ESPHome): se escriben estos
// 11 bytes (los últimos quedan en 0) y se leen 8 bytes de respuesta.
static const uint8_t AXS_READ_TOUCHPAD[11] = {
  0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00
};

// El Arduino IDE auto-genera los prototipos de función y los inserta
// ANTES de la primera función del archivo (leerToque, justo abajo) —
// por eso este struct, usado recién mucho más abajo por el teclado del
// "QR de empleado", tiene que estar declarado ACÁ (antes de la primera
// función) y no al lado de donde se usa: si no, el prototipo
// auto-generado de layoutTecladoPin() queda apuntando a un tipo que
// todavía no existe y la compilación falla con "BotonPin was not
// declared in this scope".
struct BotonPin {
  const char *etiqueta;
  int x, y, w, h;
};

/** Lee el touch. Devuelve false si no hay ningún dedo apoyado (o si el
 * I2C falló) — nunca bloquea más que una transacción I2C normal. */
bool leerToque(int &xOut, int &yOut) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(AXS_READ_TOUCHPAD, sizeof(AXS_READ_TOUCHPAD));
  if (Wire.endTransmission(true) != 0) return false;

  int leidos = Wire.requestFrom((int)TOUCH_I2C_ADDR, 8);
  if (leidos != 8) return false;

  uint8_t datos[8];
  for (int i = 0; i < 8; i++) datos[i] = Wire.read();

  if (datos[0] != 0 || datos[1] == 0) return false; // sin toque

  int x = ((datos[2] & 0x0F) << 8) | datos[3];
  int y = ((datos[4] & 0x0F) << 8) | datos[5];

  if (TOUCH_SWAP_XY) { int tmp = x; x = y; y = tmp; }
  if (TOUCH_INVERT_X) x = SCREEN_WIDTH - 1 - x;
  if (TOUCH_INVERT_Y) y = SCREEN_HEIGHT - 1 - y;

  xOut = x;
  yOut = y;
  return true;
}

// ============================================================================
//                    PALETA DE MARCA (NexoQR) — RGB565
// ============================================================================
// Mismos valores que el logo/mockup web (#1a3fa0 / #3b6fe0 / #16233b),
// convertidos a RGB565 para la pantalla.
#define COLOR_NEGRO      0x0000
#define COLOR_BLANCO     0xFFFF
#define COLOR_VERDE      0x07E0
#define COLOR_ROJO       0xF800
#define COLOR_GRIS       0x7BEF
#define COLOR_AMARILLO   0xFFE0
#define COLOR_MARCA_AZUL      0x19F4 // #1a3fa0
#define COLOR_MARCA_AZUL_CLARO 0x3B7C // #3b6fe0
#define COLOR_MARCA_NAVY       0x1107 // #16233b — fondo del isotipo/header
#define COLOR_TEXTO_MUTED       0x6B90 // #6b7280 — gris de las bajadas
#define COLOR_FONDO_FOOTER      0xF79E // #f8f9fb aprox — fondo del footer
#define COLOR_FONDO_ICONO       0xE73E // #eef2ff aprox — circulo detras de cada paso

// ============================================================================
//                          BLINDAJE / HARDENING
// ============================================================================
static const bool DEBUG_LOG = true;
static const uint32_t WDT_TIMEOUT_S = 30;
static const uint32_t WDT_TIMEOUT_PORTAL_S = 90;
static const unsigned long WIFI_DOWN_RESTART_MS = 5UL * 60UL * 1000UL; // 5 min
static const unsigned long RESTART_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL; // 12 h
static const uint32_t HEAP_SAFETY_THRESHOLD = 20000; // bytes
static const unsigned long POLL_INTERVAL_MS = 15000;

unsigned long lastPollAt = 0;
unsigned long lastWifiOkAt = 0;
unsigned long bootAt = 0;

uint32_t pollOkCount = 0;
uint32_t pollFailCount = 0;

// ============================================================================
//                          ESTADO DEL QR / MÁQUINA
// ============================================================================
String pollStatus = "";
String cachedQrData = "";
String cachedMode = "fijo";
double cachedMontoFijo = 0;
bool devicePaused = false;

// "QR de empleado" (ver migración 0015) — totalmente separado del QR
// principal de arriba, nunca se mezclan. cachedEmpleadoPin vacío =
// función desactivada para esta máquina (el dueño no la configuró).
String cachedEmpleadoPin = "";
String cachedEmpleadoQrData = "";
double cachedEmpleadoMontoFijo = 0;
// Ver migración 0017 — true mientras la cafetera está limpiando. Tiene
// prioridad sobre el QR (normal y oferta): no tiene sentido mostrar
// ninguno de los dos si nadie va a recibir su café hasta que termine.
bool cachedIsCleaning = false;

// ============================================================================
//                          ESTADO DE PANTALLA
// ============================================================================
enum DisplayState {
  SHOW_BOOT,
  SHOW_QR,
  SHOW_OFFLINE,
  SHOW_PAUSADA,
  SHOW_LIMPIANDO,
  SHOW_SIN_CODIGO,
  SHOW_CODIGO_INVALIDO,
  SHOW_CONFIG_PORTAL
};
DisplayState currentState = SHOW_BOOT;
DisplayState previousState = SHOW_BOOT;

// ============================================================================
//        OVERLAY "QR DE EMPLEADO" (teclado de PIN + QR de empleado)
// ============================================================================
// Capa INDEPENDIENTE del estado normal de arriba: sólo puede activarse
// mientras currentState == SHOW_QR (nunca interrumpe boot/offline/pausa/
// portal/etc.), y se corta sola de inmediato si currentState deja de ser
// SHOW_QR por cualquier motivo (ver actualizarPantalla) — el estado real
// de la máquina siempre tiene prioridad sobre este overlay.
enum OverlayMode { OVERLAY_NONE, OVERLAY_PIN, OVERLAY_EMPLEADO_QR, OVERLAY_PAGO_ACREDITADO };
OverlayMode overlayActual = OVERLAY_NONE;
OverlayMode overlayAnterior = OVERLAY_NONE;

String pinIngresado = "";
int intentosFallidosPin = 0;
unsigned long bloqueadoPinHasta = 0;
unsigned long overlayEmpleadoDesde = 0;

static const int MAX_INTENTOS_PIN = 3;
static const unsigned long DUR_BLOQUEO_PIN_MS = 30000;
static const unsigned long DUR_QR_EMPLEADO_MS = 30000;
static const unsigned long DEBOUNCE_TOQUE_MS = 250;

bool touchTocadoAntes = false;
unsigned long ultimoToqueRegistradoAt = 0;

// ============================================================================
//                 OVERLAY "PAGO ACREDITADO" (aviso automático)
// ============================================================================
// A diferencia del overlay de arriba, éste no lo dispara un toque sino
// hacerPoll(): cada vez que devices.last_payment_at (ver migración 0016)
// cambia respecto del último valor visto, se dispara un aviso de "Pago
// acreditado" por unos segundos y se vuelve sola al ciclo normal. Sirve
// tanto para un pago del QR principal como del QR oferta — no distingue
// cuál, sólo "hubo un pago nuevo".
String cachedUltimoPagoAt = "";
bool ultimoPagoInicializado = false;
bool pagoAcreditadoPendiente = false;
unsigned long overlayPagoAcreditadoDesde = 0;
static const unsigned long DUR_PAGO_ACREDITADO_MS = 6000;

// ============================================================================
//                          WIFIMANAGER (sólo bajo demanda)
// ============================================================================
WiFiManager wm;

// ============================================================================
//            PERSISTENCIA MANUAL DE CREDENCIALES WIFI + CÓDIGO
// ============================================================================
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

String pairingCode = "";

// ============================================================================
//                          UTILIDADES DE LOG
// ============================================================================
void logLineFwd(const String &s) {
  if (DEBUG_LOG) Serial.println(s);
}
#define logLine logLineFwd

// ============================================================================
//                    TEXTO / FORMATO
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

void textoEn(const String &texto, int x, int y, uint16_t color, uint8_t size) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(texto);
}

/** "$1.200" — separador de miles con punto, sin decimales (los montos de
 * fichas/café siempre son números redondos en pesos). */
String formatearMonto(double monto) {
  long entero = (long)(monto + 0.5);
  String digitos = String(entero);
  String resultado = "";
  int contador = 0;
  for (int i = digitos.length() - 1; i >= 0; i--) {
    resultado = digitos[i] + resultado;
    contador++;
    if (contador % 3 == 0 && i != 0) resultado = "." + resultado;
  }
  return "$" + resultado;
}

// ============================================================================
//                    ISOTIPO NexoQR (marcadores de esquina de QR)
// ============================================================================
// Con el rebrand a NexoQR, el motivo de la garra+peluche (que era de la
// marca vieja, QR&CA) ya no corresponde. El logo real de NexoQR (el que
// se muestra entero en el slideshow, ver RUTA_LOGO_PROPIO) usa marcadores
// de esquina de QR en azul/violeta — este ícono dibujado a mano reproduce
// ese mismo motivo en un solo color (la librería gráfica no tiene
// degradé), para usarlo chico en el header y en la pantalla de boot.
void dibujarLogoNexoQR(int x, int y, int size, uint16_t color) {
  int lado = size * 7 / 24;
  int offA = size * 3 / 24;
  int offB = size * 14 / 24;
  int radio = max(1, lado / 4);
  int grosor = max(1, size / 14);
  for (int g = 0; g < grosor; g++) {
    gfx->drawRoundRect(x + offA - g, y + offA - g, lado + g * 2, lado + g * 2, radio, color);
    gfx->drawRoundRect(x + offB - g, y + offA - g, lado + g * 2, lado + g * 2, radio, color);
    gfx->drawRoundRect(x + offA - g, y + offB - g, lado + g * 2, lado + g * 2, radio, color);
  }
}

// ============================================================================
//                    ÍCONOS DEL PASO A PASO (sólo formas, sin fuentes)
// ============================================================================
void iconoCamara(int cx, int cy, uint16_t color) {
  gfx->fillRoundRect(cx - 9, cy - 6, 18, 12, 2, color);
  gfx->fillRect(cx - 3, cy - 9, 6, 4, color);
  gfx->fillCircle(cx, cy, 4, COLOR_FONDO_ICONO);
  gfx->drawCircle(cx, cy, 4, color);
}

void iconoTelefono(int cx, int cy, uint16_t color) {
  gfx->drawRoundRect(cx - 7, cy - 11, 14, 22, 3, color);
  gfx->drawRoundRect(cx - 7, cy - 11, 14, 22, 3, color); // doble trazo = mas grosor
  gfx->drawLine(cx - 3, cy - 8, cx + 3, cy - 8, color);
  gfx->fillCircle(cx, cy + 7, 1.6, color);
}

void iconoCafe(int cx, int cy, uint16_t color) {
  gfx->fillRoundRect(cx - 8, cy - 6, 14, 12, 2, color);
  // asa (forma de "D" hecha con un rect sin relleno, pegada al costado)
  gfx->drawRoundRect(cx + 5, cy - 3, 7, 8, 3, color);
  // vapor
  gfx->drawLine(cx - 3, cy - 10, cx - 1, cy - 13, color);
  gfx->drawLine(cx + 1, cy - 10, cx + 3, cy - 13, color);
}

// ============================================================================
//                          PANTALLAS
// ============================================================================
void dibujarHeader() {
  // Fondo blanco de punta a punta — el header NO es una franja de color
  // (eso fue lo que no gustó en el primer intento, el salto duro entre la
  // franja oscura y el QR blanco de abajo quedaba feo). Sólo el ícono va
  // en un cuadradito con fondo propio, igual que en el mockup aprobado.
  const int badgeSize = ICONO_SIZE;
  const int badgeX = 70;
  const int badgeY = 14;
  gfx->fillRoundRect(badgeX, badgeY, badgeSize, badgeSize, 10, COLOR_MARCA_NAVY);
  dibujarIconoMarca(badgeX, badgeY, badgeSize, COLOR_BLANCO);

  int textX = badgeX + badgeSize + 10;
  int textY = badgeY + 20; // centrado con el badge mas alto (64px)
  textoEn("Nexo", textX, textY, COLOR_NEGRO, 3);
  int16_t x1, y1; uint16_t w, h;
  gfx->setTextSize(3);
  gfx->getTextBounds("Nexo", 0, 0, &x1, &y1, &w, &h);
  textoEn("QR", textX + w, textY, COLOR_MARCA_AZUL_CLARO, 3);

  // La bajada y la linea separadora van debajo del badge (que ahora, al
  // usar el isotipo real de 64px, es mas alto que el que tenia antes).
  centrarTexto("Escanea y paga", 86, COLOR_TEXTO_MUTED, 1);
  centrarTexto("con Mercado Pago", 98, COLOR_TEXTO_MUTED, 1);
  gfx->drawLine(0, 116, SCREEN_WIDTH, 116, 0xE71C);
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

/** Dibuja el QR dentro de un cuadro de maxSizePx, arrancando en offsetYTop.
 * Devuelve el lado real ocupado (con su zona de quiet). */
int dibujarQREnCuadro(const String &data, int maxSizePx, int offsetYTop) {
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

  const int QUIET_ZONE_MODULES = 3;
  int totalModulosConZona = qrcode.size + QUIET_ZONE_MODULES * 2;
  int pixelPorModulo = maxSizePx / totalModulosConZona;
  if (pixelPorModulo < 1) pixelPorModulo = 1;

  int totalPxQR = pixelPorModulo * qrcode.size;
  int cuadroLado = totalPxQR + pixelPorModulo * QUIET_ZONE_MODULES * 2;
  int cuadroX = (SCREEN_WIDTH - cuadroLado) / 2;

  gfx->fillRoundRect(cuadroX, offsetYTop, cuadroLado, cuadroLado, 6, COLOR_BLANCO);
  gfx->drawRoundRect(cuadroX, offsetYTop, cuadroLado, cuadroLado, 6, COLOR_GRIS);

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

void dibujarFooter() {
  // OJO alturas: todo este bloque tiene que entrar entre footerY y
  // SCREEN_HEIGHT (480) — la primera versión calculaba el texto de los
  // pasos por fuera de la pantalla (se dibujaba, pero en coordenadas
  // y > 480, así que nunca se veía: sólo quedaban los íconos a la
  // vista). Estos offsets son todos FIJOS relativos a footerY, pensados
  // para que el peor caso (con monto mostrado) entre con margen.
  const int footerY = 356;
  gfx->fillRect(0, footerY, SCREEN_WIDTH, SCREEN_HEIGHT - footerY, COLOR_FONDO_FOOTER);
  gfx->drawLine(0, footerY, SCREEN_WIDTH, footerY, 0xE71C);

  // Monto — sólo tiene sentido en modo "fijo" (en "combo" el cliente ve
  // el menú de fichas pegado al lado de la máquina, no acá). En "combo"
  // simplemente no se dibuja nada acá arriba y el paso a paso queda con
  // un poco más de aire.
  if (cachedMode == "fijo" && cachedMontoFijo > 0) {
    centrarTexto("MONTO A PAGAR", footerY + 10, COLOR_TEXTO_MUTED, 1);
    centrarTexto(formatearMonto(cachedMontoFijo), footerY + 22, COLOR_NEGRO, 3);
  }

  // Paso a paso — stepY tiene que quedar por debajo de donde termina el
  // valor del monto ("$100" a tamaño de texto 3, dibujado en footerY+22,
  // que ocupa ~24px de alto => termina cerca de footerY+46). Con stepY en
  // footerY+54 el circulo del icono (radio 17, arranca en stepY-17) se
  // superponia con ese texto. footerY+78 deja ~15px de aire y el paso a
  // paso completo (circulo + 2 lineas de texto debajo) sigue entrando
  // dentro de SCREEN_HEIGHT con margen.
  int stepY = footerY + 78;
  int colW = SCREEN_WIDTH / 3;
  const char *labels[3][2] = {
    {"Escanea", "el QR"},
    {"Paga con", "la app"},
    {"Retira tu", "cafe"}
  };
  for (int i = 0; i < 3; i++) {
    int cx = colW * i + colW / 2;
    gfx->fillCircle(cx, stepY, 17, COLOR_FONDO_ICONO);
    if (i == 0) iconoCamara(cx, stepY, COLOR_MARCA_AZUL);
    else if (i == 1) iconoTelefono(cx, stepY, COLOR_MARCA_AZUL);
    else iconoCafe(cx, stepY, COLOR_MARCA_AZUL);

    gfx->setTextSize(1);
    gfx->setTextColor(0x39C7); // gris oscuro, texto chico
    int16_t x1, y1; uint16_t w, h;
    gfx->getTextBounds(labels[i][0], 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(cx - w / 2, stepY + 24);
    gfx->print(labels[i][0]);
    gfx->getTextBounds(labels[i][1], 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(cx - w / 2, stepY + 34);
    gfx->print(labels[i][1]);
  }
}

// ============================================================================
//               microSD — montaje e imagenes desde tarjeta
// ============================================================================
String sdEstadoTexto = "SD: sin revisar";
String logosEstadoTexto = "";

/** Chequeo rapido de que un .BIN este donde el firmware lo espera y con el
 * tamano exacto (no decodifica nada, sólo abre y mira el tamano) -- para
 * poder mostrar en pantalla, sin mirar el log serie, si falta copiar
 * algun archivo a la SD. */
String verificarArchivoBin(const char *ruta, size_t esperado) {
  if (!SD.exists(ruta)) return "FALTA";
  File f = SD.open(ruta, FILE_READ);
  if (!f) return "FALTA";
  size_t tam = f.size();
  f.close();
  if (tam != esperado) return "TAM.MAL";
  return "OK";
}

void verificarArchivosLogos() {
  const size_t esperadoLogo = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT * 2;
  const size_t esperadoIcono = (size_t)ICONO_SIZE * ICONO_SIZE * 2;
  String e1 = verificarArchivoBin(RUTA_LOGO_CLIENTE, esperadoLogo);
  String e2 = verificarArchivoBin(RUTA_LOGO_PROPIO, esperadoLogo);
  String e3 = verificarArchivoBin(RUTA_ICONO, esperadoIcono);
  logosEstadoTexto = "L1:" + e1 + " L2:" + e2 + " ICO:" + e3;
  logLine(">> Archivos SD -- LOGO1:" + e1 + "  LOGO2:" + e2 + "  ICONO:" + e3);
}

bool inicializarSD() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (SD.begin(SD_CS, sdSPI)) {
    uint64_t tamanoMB = SD.cardSize() / (1024ULL * 1024ULL);
    logLine(">> microSD detectada OK (" + String((unsigned long)tamanoMB) + " MB).");
    sdEstadoTexto = "SD OK (" + String((unsigned long)tamanoMB) + " MB)";
    return true;
  }

  // Reintento a una frecuencia mas baja -- con tarjetas/cableado marginales
  // a veces falla al default (~4MHz) pero anda bien mas lento.
  SD.end();
  delay(50);
  if (SD.begin(SD_CS, sdSPI, 1000000)) {
    uint64_t tamanoMB = SD.cardSize() / (1024ULL * 1024ULL);
    logLine(">> microSD detectada OK a 1MHz (" + String((unsigned long)tamanoMB) + " MB).");
    sdEstadoTexto = "SD OK (1MHz)";
    return true;
  }

  logLine("!! No se detecto tarjeta microSD (probado a velocidad normal y a 1MHz).");
  logLine("   Revisar: tarjeta insertada y formateada FAT32, pines CS/SCK/MOSI/MISO");
  logLine("   (CS=" + String(SD_CS) + " SCK=" + String(SD_SCK) + " MOSI=" + String(SD_MOSI) +
          " MISO=" + String(SD_MISO) + "), y que LOGO1.BIN/LOGO2.BIN/ICONO.BIN esten en la raiz.");
  sdEstadoTexto = "SD no detectada";
  return false;
}

// ============================================================================
//   NUEVO v2.5.0 — logos subidos desde la app (ver api/display/[code]/qr)
// ============================================================================
// Cuánto vale la "version" del logo que ya tenemos guardado en la SD, para
// poder compararla contra la que manda el backend en cada poll sin tener
// que leer la SD de nuevo cada vez. Se cargan una sola vez al bootear
// (cargarVersionesLogosCacheadas(), llamada desde setup()) y se actualizan
// solas cada vez que una descarga nueva termina bien.
// IMPORTANTE: el backend manda la version como epoch en SEGUNDOS (no en
// milisegundos) a propósito — un "long" en el ESP32 es de sólo 32 bits
// (max ~2147M) y un epoch en milisegundos (13 dígitos, ~1.7 billones)
// se trunca/corrompe ahí, lo que hacía que esta pantalla nunca
// detectara el cambio de version (asi quedo el v2.5.0 original: se
// subia el logo desde la app pero la placa nunca lo bajaba). En
// segundos entra sin problema (~1.8 mil millones hoy, contra un max de
// ~2147 millones -- cómodo hasta 2038). El fix real fue del lado del
// backend (api/display/[code]/qr) — esta placa no necesita tocarse si
// ya tiene este mismo código (el de v2.5.0), sólo esperar al redeploy.
long logoClienteVersionCache = 0;
long logoNexoVersionCache = 0;

long leerVersionDesdeSD(const char *ruta) {
  if (!SD.exists(ruta)) return 0;
  File f = SD.open(ruta, FILE_READ);
  if (!f) return 0;
  String s = f.readString();
  f.close();
  return s.toInt();
}

void guardarVersionEnSD(const char *ruta, long version) {
  File f = SD.open(ruta, FILE_WRITE);
  if (!f) {
    logLine("!! logo: no se pudo guardar la version en " + String(ruta));
    return;
  }
  f.print(version);
  f.close();
}

void cargarVersionesLogosCacheadas() {
  logoClienteVersionCache = leerVersionDesdeSD("/LOGO1VER.TXT");
  logoNexoVersionCache = leerVersionDesdeSD("/LOGO2VER.TXT");
}

/** Descarga un .BIN (320x480 RGB565 crudo, mismo formato que ya usa la
 * SD) desde `url` (HTTPS, URL pública de Supabase Storage) y lo guarda
 * en `rutaSD`. Escribe primero a un archivo temporal y sólo reemplaza
 * el archivo final si la descarga terminó completa y con el tamaño
 * exacto esperado — así una descarga que se corta a mitad de camino
 * (WiFi débil, etc.) NUNCA deja el logo actual roto o a medio escribir;
 * en ese caso simplemente se reintenta en el próximo poll. */
bool descargarLogoASD(const char *url, const char *rutaSD) {
  if (url == nullptr || strlen(url) == 0) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);

  if (!http.begin(client, url)) {
    logLine("!! logo: no se pudo iniciar la descarga de " + String(url));
    return false;
  }

  int codigoHttp = http.GET();
  if (codigoHttp != 200) {
    logLine("!! logo: HTTP " + String(codigoHttp) + " descargando " + String(url));
    http.end();
    return false;
  }

  const size_t esperado = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT * 2;
  int tamanoAnunciado = http.getSize();
  if (tamanoAnunciado > 0 && (size_t)tamanoAnunciado != esperado) {
    logLine("!! logo: tamano inesperado en " + String(url) + " (" + String(tamanoAnunciado) +
            " bytes, se esperaban " + String((unsigned long)esperado) + ").");
    http.end();
    return false;
  }

  String rutaTmp = String(rutaSD) + ".tmp";
  SD.remove(rutaTmp);
  File f = SD.open(rutaTmp, FILE_WRITE);
  if (!f) {
    logLine("!! logo: no se pudo abrir " + rutaTmp + " para escribir en la SD.");
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t totalEscrito = 0;
  unsigned long tSinDatos = millis();
  const unsigned long TIMEOUT_SIN_DATOS_MS = 20000;

  while (http.connected() && totalEscrito < esperado) {
    size_t disponibles = stream->available();
    if (disponibles > 0) {
      size_t aLeer = disponibles < sizeof(buf) ? disponibles : sizeof(buf);
      size_t leidos = stream->readBytes(buf, aLeer);
      if (leidos > 0) {
        f.write(buf, leidos);
        totalEscrito += leidos;
        tSinDatos = millis();
      }
    } else {
      if (millis() - tSinDatos > TIMEOUT_SIN_DATOS_MS) {
        logLine("!! logo: timeout descargando " + String(url) + " (" +
                String((unsigned long)totalEscrito) + "/" + String((unsigned long)esperado) +
                " bytes).");
        f.close();
        SD.remove(rutaTmp);
        http.end();
        return false;
      }
      delay(5);
      esp_task_wdt_reset();
    }
  }
  f.close();
  http.end();

  if (totalEscrito != esperado) {
    logLine("!! logo: descarga incompleta de " + String(url) + " (" +
            String((unsigned long)totalEscrito) + "/" + String((unsigned long)esperado) +
            " bytes).");
    SD.remove(rutaTmp);
    return false;
  }

  SD.remove(rutaSD);
  if (!SD.rename(rutaTmp, rutaSD)) {
    logLine("!! logo: no se pudo renombrar " + rutaTmp + " a " + String(rutaSD));
    return false;
  }

  logLine(">> logo actualizado: " + String(rutaSD));
  return true;
}

/** Compara la version que acaba de llegar en el poll contra la que ya
 * tenemos guardada y, si cambió, descarga el .BIN nuevo. Si `urlNueva`
 * viene vacia/null (el dueño o el admin todavia nunca subieron nada
 * desde la app) no hace NADA -- el logo que ya este en la SD (puesto a
 * mano, como siempre) queda intacto. Si la descarga falla, tampoco
 * actualiza la version cacheada -- así se reintenta solo en el proximo
 * poll, sin bloquear ni repetir el ciclo de pantalla. */
void verificarYActualizarLogo(const char *rutaLogo, const char *rutaVersionSD,
                               long &versionCacheada, const char *urlNueva,
                               long versionNueva) {
  if (urlNueva == nullptr || strlen(urlNueva) == 0) return;
  if (versionNueva <= 0 || versionNueva == versionCacheada) return;

  logLine(">> logo: version nueva para " + String(rutaLogo) + " (" +
          String(versionCacheada) + " -> " + String(versionNueva) + "), descargando...");

  if (descargarLogoASD(urlNueva, rutaLogo)) {
    versionCacheada = versionNueva;
    guardarVersionEnSD(rutaVersionSD, versionCacheada);
    verificarArchivosLogos(); // refresca logosEstadoTexto para la pantalla de boot/diagnostico
  }
}

/** Lee un archivo .BIN en RGB565 (big-endian, byte alto primero -- el
 * mismo formato que produce el script de conversion) de w x h pixeles y
 * lo vuelca a pantalla arrancando en (x, y). Devuelve false ante
 * cualquier problema (sin romper nada: el llamador decide que hacer,
 * normalmente saltar ese paso o caer a un dibujo de respaldo). */
bool dibujarImagenDesdeSD(const char *ruta, int x, int y, int w, int h) {
  File f = SD.open(ruta, FILE_READ);
  if (!f) {
    logLine("!! No se pudo abrir " + String(ruta) + " en la SD.");
    return false;
  }

  const size_t esperado = (size_t)w * h * 2;
  if ((size_t)f.size() != esperado) {
    logLine("!! " + String(ruta) + ": tamano inesperado (" + String((unsigned long)f.size()) +
            " bytes, se esperaban " + String((unsigned long)esperado) + ").");
    f.close();
    return false;
  }

  uint8_t filaBytes[SCREEN_WIDTH * 2]; // suficiente para cualquier ancho usado
  uint16_t filaPixeles[SCREEN_WIDTH];
  size_t bytesPorFila = (size_t)w * 2;

  for (int fila = 0; fila < h; fila++) {
    size_t leidos = f.read(filaBytes, bytesPorFila);
    if (leidos != bytesPorFila) {
      logLine("!! " + String(ruta) + ": lectura incompleta en fila " + String(fila) + ".");
      f.close();
      return false;
    }
    for (int px = 0; px < w; px++) {
      filaPixeles[px] = ((uint16_t)filaBytes[px * 2] << 8) | filaBytes[px * 2 + 1];
    }
    gfx->draw16bitRGBBitmap(x, y + fila, filaPixeles, w, 1);
    if ((fila & 0x3F) == 0) esp_task_wdt_reset(); // cada 64 filas
  }

  f.close();
  return true;
}

/** Ícono chico (header / boot): usa el isotipo real recortado desde la SD
 * si está disponible; si no, cae al vector dibujado a mano como respaldo
 * -- nunca deja el espacio vacío. */
void dibujarIconoMarca(int x, int y, int size, uint16_t colorRespaldo) {
  if (sdDisponible && size == ICONO_SIZE &&
      dibujarImagenDesdeSD(RUTA_ICONO, x, y, ICONO_SIZE, ICONO_SIZE)) {
    return;
  }
  dibujarLogoNexoQR(x, y, size, colorRespaldo);
}

void dibujarPantallaQR() {
  gfx->fillScreen(COLOR_BLANCO);
  dibujarHeader();

  if (cachedQrData.length() == 0) {
    centrarTexto("Sin datos", 220, COLOR_NEGRO, 2);
  } else {
    const int footerY = 356; // debe coincidir con el de dibujarFooter()
    const int headerBottom = 122; // debe coincidir con la linea separadora de dibujarHeader() + margen
    int disponible = footerY - headerBottom; // entre el header y el footer
    int maxQrPx = min(disponible, SCREEN_WIDTH - 40);
    int lado = dibujarQREnCuadro(cachedQrData, maxQrPx, headerBottom + (disponible - maxQrPx) / 2);
    if (lado == 0) {
      centrarTexto("Error QR", 220, COLOR_NEGRO, 2);
    }
  }

  dibujarFooter();
}

// ============================================================================
//       "QR DE EMPLEADO" — teclado de PIN y pantalla del segundo QR
// ============================================================================
/** Grilla 4x3: 1-9, luego "<" (borrar) / "0" / "OK". Separado en su
 * propia función para que el dibujo y el hit-test usen EXACTAMENTE el
 * mismo layout. */
void layoutTecladoPin(BotonPin botones[12]) {
  static const char *etiquetas[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "OK"
  };
  const int colW = SCREEN_WIDTH / 3;
  const int startY = 160;
  const int rowH = 66;
  for (int i = 0; i < 12; i++) {
    int fila = i / 3;
    int col = i % 3;
    botones[i].etiqueta = etiquetas[i];
    botones[i].x = col * colW + 10;
    botones[i].y = startY + fila * rowH;
    botones[i].w = colW - 20;
    botones[i].h = rowH - 12;
  }
}

/** Devuelve la etiqueta del botón tocado ("C" = cancelar, vía la franja
 * superior), o "" si el toque no cayó sobre ningún control. */
String etiquetaBotonPin(int x, int y) {
  // Franja "Cancelar" arriba a la derecha — activa aunque esté bloqueado,
  // para poder salir del overlay sin esperar los 30s de lockout.
  if (y < 50 && x > SCREEN_WIDTH - 110) return "C";

  if (millis() < bloqueadoPinHasta) return ""; // teclado inactivo mientras dura el bloqueo

  BotonPin botones[12];
  layoutTecladoPin(botones);
  for (int i = 0; i < 12; i++) {
    if (x >= botones[i].x && x < botones[i].x + botones[i].w &&
        y >= botones[i].y && y < botones[i].y + botones[i].h) {
      return String(botones[i].etiqueta);
    }
  }
  return "";
}

void dibujarPantallaPin() {
  gfx->fillScreen(COLOR_MARCA_NAVY);
  centrarTexto("QR oferta", 36, COLOR_BLANCO, 2);
  textoEn("Cancelar", SCREEN_WIDTH - 92, 16, COLOR_GRIS, 1);

  if (millis() < bloqueadoPinHasta) {
    unsigned long restanteS = (bloqueadoPinHasta - millis()) / 1000 + 1;
    centrarTexto("Bloqueado", 210, COLOR_ROJO, 3);
    centrarTexto("Esperá " + String((unsigned long)restanteS) + "s", 250, COLOR_GRIS, 2);
    return;
  }

  String puntos = "";
  for (unsigned int i = 0; i < pinIngresado.length(); i++) puntos += "* ";
  centrarTexto(puntos.length() > 0 ? puntos : "- - - -", 92, COLOR_BLANCO, 3);

  BotonPin botones[12];
  layoutTecladoPin(botones);
  for (int i = 0; i < 12; i++) {
    uint16_t colorBoton = (String(botones[i].etiqueta) == "OK") ? COLOR_VERDE : COLOR_MARCA_AZUL_CLARO;
    gfx->fillRoundRect(botones[i].x, botones[i].y, botones[i].w, botones[i].h, 8, colorBoton);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_BLANCO);
    int16_t x1, y1; uint16_t w, h;
    gfx->getTextBounds(botones[i].etiqueta, 0, 0, &x1, &y1, &w, &h);
    int tx = botones[i].x + (botones[i].w - (int)w) / 2;
    int ty = botones[i].y + (botones[i].h + (int)h) / 2;
    gfx->setCursor(tx, ty);
    gfx->print(botones[i].etiqueta);
  }
}

void dibujarPantallaQREmpleado() {
  gfx->fillScreen(COLOR_BLANCO);
  dibujarHeader();
  centrarTexto("QR OFERTA", 128, COLOR_MARCA_AZUL, 2);

  const int footerY = 356;
  const int headerBottom = 150;

  if (cachedEmpleadoQrData.length() == 0) {
    centrarTexto("Sin datos", 220, COLOR_NEGRO, 2);
  } else {
    int disponible = footerY - headerBottom;
    int maxQrPx = min(disponible, SCREEN_WIDTH - 40);
    int lado = dibujarQREnCuadro(cachedEmpleadoQrData, maxQrPx, headerBottom + (disponible - maxQrPx) / 2);
    if (lado == 0) centrarTexto("Error QR", 220, COLOR_NEGRO, 2);
  }

  gfx->fillRect(0, footerY, SCREEN_WIDTH, SCREEN_HEIGHT - footerY, COLOR_FONDO_FOOTER);
  gfx->drawLine(0, footerY, SCREEN_WIDTH, footerY, 0xE71C);
  if (cachedEmpleadoMontoFijo > 0) {
    centrarTexto("MONTO OFERTA", footerY + 16, COLOR_TEXTO_MUTED, 1);
    centrarTexto(formatearMonto(cachedEmpleadoMontoFijo), footerY + 30, COLOR_NEGRO, 3);
  }
  centrarTexto("Volviendo al QR normal...", footerY + 70, COLOR_TEXTO_MUTED, 1);
}

// ============================================================================
//                    AVISO "PAGO ACREDITADO" (taza de café)
// ============================================================================
/** Versión grande (para la pantalla de "Pago acreditado") del mismo
 * motivo que iconoCafe() dibuja chico en el paso a paso del footer —
 * taza + asa + platito + vapor, todo a mano (sin fuentes/emoji). */
void iconoCafeGrande(int cx, int cy, uint16_t color) {
  // Platito
  gfx->fillRoundRect(cx - 55, cy + 38, 110, 12, 6, color);
  // Taza
  gfx->fillRoundRect(cx - 42, cy - 28, 78, 62, 12, color);
  // Asa (doble trazo = más grosor, sin relleno para que se vea "hueca")
  gfx->drawRoundRect(cx + 30, cy - 8, 34, 40, 15, color);
  gfx->drawRoundRect(cx + 31, cy - 7, 32, 38, 14, color);
  // Vapor — 3 trazos en zigzag
  for (int i = -1; i <= 1; i++) {
    int ox = cx + i * 20;
    gfx->drawLine(ox - 7, cy - 38, ox + 2, cy - 54, color);
    gfx->drawLine(ox + 2, cy - 54, ox - 7, cy - 70, color);
  }
}

void dibujarPantallaPagoAcreditado() {
  gfx->fillScreen(COLOR_VERDE);
  iconoCafeGrande(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 100, COLOR_BLANCO);
  centrarTexto("Pago acreditado", SCREEN_HEIGHT / 2 + 20, COLOR_BLANCO, 3);
  centrarTexto("Disfruta tu cafe", SCREEN_HEIGHT / 2 + 64, COLOR_BLANCO, 2);
  centrarTexto("NexoQR", SCREEN_HEIGHT - 40, COLOR_BLANCO, 2);
}

/** Maneja un toque "nuevo" (ya con el debounce aplicado por el caller) de
 * acuerdo al overlay activo. Nunca toca cachedQrData/cachedMode/etc del
 * QR principal — sólo lee/escribe las variables del overlay. */
void manejarToque(int x, int y) {
  if (overlayActual == OVERLAY_NONE) {
    // Tocar la pantalla sólo abre el teclado de PIN si: estamos
    // mostrando normalmente el QR de cliente, y esta máquina tiene un
    // PIN de empleado configurado. En cualquier otro estado (boot,
    // offline, pausada, sin código...) tocar no hace nada.
    if (currentState != SHOW_QR) return;
    if (cachedEmpleadoPin.length() == 0) return;

    overlayActual = OVERLAY_PIN;
    pinIngresado = "";
    return;
  }

  if (overlayActual == OVERLAY_PIN) {
    String etiqueta = etiquetaBotonPin(x, y);
    if (etiqueta.length() == 0) return;

    if (etiqueta == "C") {
      overlayActual = OVERLAY_NONE;
      return;
    }
    if (millis() < bloqueadoPinHasta) return; // teclado bloqueado, sólo "Cancelar" responde

    if (etiqueta == "<") {
      if (pinIngresado.length() > 0) pinIngresado.remove(pinIngresado.length() - 1);
    } else if (etiqueta == "OK") {
      if (pinIngresado.length() > 0 && pinIngresado == cachedEmpleadoPin) {
        intentosFallidosPin = 0;
        overlayActual = OVERLAY_EMPLEADO_QR;
        overlayEmpleadoDesde = millis();
      } else {
        intentosFallidosPin++;
        pinIngresado = "";
        if (intentosFallidosPin >= MAX_INTENTOS_PIN) {
          bloqueadoPinHasta = millis() + DUR_BLOQUEO_PIN_MS;
          intentosFallidosPin = 0;
        }
      }
    } else if (pinIngresado.length() < 6) {
      pinIngresado += etiqueta;
    }
    return;
  }

  if (overlayActual == OVERLAY_EMPLEADO_QR) {
    // Tocar en cualquier lado durante el QR de empleado corta el ciclo
    // antes de los 30s y vuelve directo al QR normal.
    overlayActual = OVERLAY_NONE;
  }
}

void dibujarPantallaBoot(const String &msg) {
  gfx->fillScreen(COLOR_MARCA_NAVY);
  int iconSize = ICONO_SIZE;
  dibujarIconoMarca(SCREEN_WIDTH / 2 - iconSize / 2, SCREEN_HEIGHT / 2 - 104, iconSize, COLOR_BLANCO);
  centrarTexto("NexoQR", SCREEN_HEIGHT / 2 - 6, COLOR_BLANCO, 3);
  centrarTexto(msg, SCREEN_HEIGHT / 2 + 30, COLOR_TEXTO_MUTED, 1);
  gfx->flush();
}

void dibujarPantallaOffline() {
  gfx->fillScreen(COLOR_ROJO);
  centrarTexto("Fuera de", SCREEN_HEIGHT / 2 - 40, COLOR_BLANCO, 3);
  centrarTexto("Servicio", SCREEN_HEIGHT / 2 + 10, COLOR_BLANCO, 3);
  centrarTexto("NexoQR", SCREEN_HEIGHT - 50, COLOR_BLANCO, 2);
}

void dibujarPantallaPausada() {
  gfx->fillScreen(COLOR_MARCA_NAVY);
  centrarTexto("Maquina en", SCREEN_HEIGHT / 2 - 30, COLOR_GRIS, 3);
  centrarTexto("pausa", SCREEN_HEIGHT / 2 + 20, COLOR_GRIS, 3);
  centrarTexto("NexoQR", SCREEN_HEIGHT - 50, COLOR_MARCA_AZUL_CLARO, 2);
}

// Ver migración 0017 / NOVEDAD v2.4.0. Mismo estilo que dibujarPantallaPausada
// (texto centrado sobre fondo sólido) pero en amarillo, para que se note a
// simple vista que es algo temporal (la máquina vuelve sola) y no un estado
// de error como SHOW_OFFLINE (rojo) ni una pausa puesta a mano (navy).
void dibujarPantallaLimpiando() {
  gfx->fillScreen(COLOR_AMARILLO);
  centrarTexto("Maquina en", SCREEN_HEIGHT / 2 - 30, COLOR_NEGRO, 3);
  centrarTexto("limpieza", SCREEN_HEIGHT / 2 + 20, COLOR_NEGRO, 3);
  centrarTexto("Volvemos en un momento", SCREEN_HEIGHT / 2 + 64, COLOR_NEGRO, 1);
  centrarTexto("NexoQR", SCREEN_HEIGHT - 40, COLOR_NEGRO, 2);
}

void dibujarPantallaSinCodigo() {
  gfx->fillScreen(COLOR_GRIS);
  centrarTexto("Falta cargar", SCREEN_HEIGHT / 2 - 50, COLOR_NEGRO, 2);
  centrarTexto("codigo de", SCREEN_HEIGHT / 2 - 20, COLOR_NEGRO, 2);
  centrarTexto("emparejamiento", SCREEN_HEIGHT / 2 + 10, COLOR_NEGRO, 2);
  centrarTexto("(boton BOOT 2s)", SCREEN_HEIGHT / 2 + 50, COLOR_NEGRO, 2);
  centrarTexto("NexoQR", SCREEN_HEIGHT - 50, COLOR_MARCA_AZUL, 2);
}

void dibujarPantallaCodigoInvalido() {
  gfx->fillScreen(COLOR_ROJO);
  centrarTexto("Codigo de", SCREEN_HEIGHT / 2 - 50, COLOR_BLANCO, 2);
  centrarTexto("emparejamiento", SCREEN_HEIGHT / 2 - 20, COLOR_BLANCO, 2);
  centrarTexto("invalido", SCREEN_HEIGHT / 2 + 10, COLOR_BLANCO, 2);
  centrarTexto("(boton BOOT 2s)", SCREEN_HEIGHT / 2 + 50, COLOR_BLANCO, 2);
  centrarTexto("NexoQR", SCREEN_HEIGHT - 50, COLOR_BLANCO, 2);
}

void dibujarPantallaConfigPortal() {
  gfx->fillScreen(COLOR_MARCA_AZUL);
  centrarTexto("Modo config.", SCREEN_HEIGHT / 2 - 50, COLOR_BLANCO, 3);
  centrarTexto("Conectate a:", SCREEN_HEIGHT / 2, COLOR_BLANCO, 2);
  centrarTexto("NexoQR_Display", SCREEN_HEIGHT / 2 + 35, COLOR_AMARILLO, 2);
}

// ============================================================================
//     CICLO DE SLIDESHOW (sólo corre mientras currentState == SHOW_QR)
// ============================================================================
// Logo cliente (2s) -> QR (10s) -> Logo propio (2s) -> QR (10s) -> repite.
// Si no hay SD o falta algun archivo de logo, ese paso se salta solo
// (mostrarLogoPantallaCompleta devuelve false) y se pasa directo al QR -- el
// slideshow nunca bloquea ni degrada el funcionamiento normal del cobro.
enum PasoSlideshow {
  PASO_LOGO_CLIENTE,
  PASO_QR_1,
  PASO_LOGO_PROPIO,
  PASO_QR_2
};
PasoSlideshow pasoActual = PASO_LOGO_CLIENTE;
unsigned long pasoDesde = 0;
static const unsigned long DUR_LOGO_MS = 2000;
static const unsigned long DUR_QR_MS = 10000;

bool mostrarLogoPantallaCompleta(const char *ruta) {
  if (!sdDisponible) return false;
  bool ok = dibujarImagenDesdeSD(ruta, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  if (ok) gfx->flush();
  return ok;
}

void dibujarPasoActual() {
  switch (pasoActual) {
    case PASO_LOGO_CLIENTE:
      if (mostrarLogoPantallaCompleta(RUTA_LOGO_CLIENTE)) return;
      pasoActual = PASO_QR_1; // sin SD/archivo -> saltamos directo al QR
      // cae al case de abajo
    case PASO_QR_1:
      dibujarPantallaQR();
      gfx->flush();
      break;
    case PASO_LOGO_PROPIO:
      if (mostrarLogoPantallaCompleta(RUTA_LOGO_PROPIO)) return;
      pasoActual = PASO_QR_2;
      // cae al case de abajo
    case PASO_QR_2:
      dibujarPantallaQR();
      gfx->flush();
      break;
  }
}

void actualizarCicloQR() {
  bool esPasoDeLogo = (pasoActual == PASO_LOGO_CLIENTE || pasoActual == PASO_LOGO_PROPIO);
  unsigned long duracion = esPasoDeLogo ? DUR_LOGO_MS : DUR_QR_MS;

  if (millis() - pasoDesde >= duracion) {
    pasoActual = (PasoSlideshow)((pasoActual + 1) % 4);
    pasoDesde = millis();
    dibujarPasoActual();
  }
}

void actualizarPantalla() {
  // Salvoconducto de seguridad: el estado REAL de la máquina (pagada,
  // pausada, offline...) siempre tiene prioridad sobre el overlay de
  // empleado — si currentState deja de ser SHOW_QR por cualquier motivo
  // mientras había un PIN o un QR de empleado en pantalla, se corta ya.
  if (overlayActual != OVERLAY_NONE && currentState != SHOW_QR) {
    overlayActual = OVERLAY_NONE;
  }

  // "Pago acreditado" — sólo se dispara si no hay YA otro overlay
  // activo (nunca interrumpe un PIN a medio escribir ni un QR de
  // empleado ya destapado) y sólo mientras se está mostrando
  // normalmente el QR de cliente. Si el flag queda pendiente porque
  // justo había otro overlay, se consume solo en un frame posterior
  // apenas ese overlay se cierra.
  if (overlayActual == OVERLAY_NONE && pagoAcreditadoPendiente && currentState == SHOW_QR) {
    pagoAcreditadoPendiente = false;
    overlayActual = OVERLAY_PAGO_ACREDITADO;
    overlayPagoAcreditadoDesde = millis();
  }

  if (overlayActual == OVERLAY_PAGO_ACREDITADO) {
    if (millis() - overlayPagoAcreditadoDesde >= DUR_PAGO_ACREDITADO_MS) {
      overlayActual = OVERLAY_NONE;
      overlayAnterior = OVERLAY_NONE;
      previousState = SHOW_BOOT; // fuerza a que el ciclo normal se redibuje desde el principio
    } else {
      if (overlayAnterior != OVERLAY_PAGO_ACREDITADO) {
        overlayAnterior = OVERLAY_PAGO_ACREDITADO;
        dibujarPantallaPagoAcreditado();
        gfx->flush();
      }
      return;
    }
  }

  if (overlayActual == OVERLAY_PIN) {
    bool recienEntramos = (overlayAnterior != OVERLAY_PIN);
    overlayAnterior = OVERLAY_PIN;
    static unsigned long ultimoRedibujoBloqueo = 0;
    bool bloqueado = millis() < bloqueadoPinHasta;
    if (recienEntramos || !bloqueado ||
        (bloqueado && millis() - ultimoRedibujoBloqueo > 1000)) {
      if (bloqueado) ultimoRedibujoBloqueo = millis();
      dibujarPantallaPin();
      gfx->flush();
    }
    return;
  }

  if (overlayActual == OVERLAY_EMPLEADO_QR) {
    if (millis() - overlayEmpleadoDesde >= DUR_QR_EMPLEADO_MS) {
      // Se cumplieron los 30s: soltamos el overlay y forzamos que el
      // ciclo normal (logos + QR de cliente) se redibuje desde el
      // principio, como si recién hubiéramos vuelto al estado SHOW_QR.
      overlayActual = OVERLAY_NONE;
      overlayAnterior = OVERLAY_NONE;
      previousState = SHOW_BOOT;
    } else {
      if (overlayAnterior != OVERLAY_EMPLEADO_QR) {
        overlayAnterior = OVERLAY_EMPLEADO_QR;
        dibujarPantallaQREmpleado();
        gfx->flush();
      }
      return;
    }
  } else if (overlayActual == OVERLAY_NONE) {
    overlayAnterior = OVERLAY_NONE;
  }

  if (currentState == SHOW_QR) {
    if (previousState != SHOW_QR) {
      // Recién entramos al estado "QR" (veníamos de offline/pausa/etc.) ->
      // arrancamos el ciclo del slideshow desde el principio.
      previousState = SHOW_QR;
      pasoActual = PASO_LOGO_CLIENTE;
      pasoDesde = millis();
      dibujarPasoActual();
    }
    actualizarCicloQR();
    return;
  }

  if (currentState == previousState) return;
  previousState = currentState;

  switch (currentState) {
    case SHOW_BOOT: return; // dibujada y mandada a pantalla a mano
    case SHOW_QR: return;   // manejado arriba
    case SHOW_OFFLINE: dibujarPantallaOffline(); break;
    case SHOW_PAUSADA: dibujarPantallaPausada(); break;
    case SHOW_LIMPIANDO: dibujarPantallaLimpiando(); break;
    case SHOW_SIN_CODIGO: dibujarPantallaSinCodigo(); break;
    case SHOW_CODIGO_INVALIDO: dibujarPantallaCodigoInvalido(); break;
    case SHOW_CONFIG_PORTAL: dibujarPantallaConfigPortal(); break;
  }
  gfx->flush();
}

// ============================================================================
//          CONSULTA AL BACKEND (socket TLS crudo, nunca HTTPClient)
// ============================================================================
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
  payload.reserve(1024);
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

  // Subido de 768 a 1536 (dos QR, principal + empleado) y ahora a 2048
  // v2.5.0: la respuesta puede traer además hasta 2 URLs de logos
  // (Supabase Storage, ~100-150 caracteres cada una) — ver migración
  // 0019.
  StaticJsonDocument<2048> doc;
  int httpStatus = -1;
  bool ok = consultarQR(pairingCode, doc, httpStatus);

  if (!ok) {
    pollFailCount++;
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
  const char *modo = doc["mode"] | "fijo";
  cachedMode = String(modo);
  cachedMontoFijo = doc["mp_monto_fijo"] | 0.0;

  // "QR de empleado" — campos nuevos, todos opcionales (ver migración
  // 0015 y api/display/[code]/qr). Si el dueño no lo configuró, vienen
  // null y cachedEmpleadoPin queda vacío -> manejarToque() ignora los
  // toques en pantalla para esta máquina.
  const char *empPin = doc["empleado_pin"] | (const char *)nullptr;
  cachedEmpleadoPin = (empPin != nullptr) ? String(empPin) : "";
  const char *empQr = doc["empleado_qr_data"] | (const char *)nullptr;
  cachedEmpleadoQrData = (empQr != nullptr) ? String(empQr) : "";
  cachedEmpleadoMontoFijo = doc["empleado_mp_monto_fijo"] | 0.0;

  // "Pago acreditado" (ver migración 0016) — detección por cambio de
  // valor, no por contenido: el primer poll después de bootear sólo
  // establece la base (nunca dispara el aviso, aunque last_payment_at
  // ya venga con algo de una venta vieja); recién un valor DISTINTO del
  // que ya conocíamos dispara pagoAcreditadoPendiente.
  const char *ultimoPago = doc["last_payment_at"] | (const char *)nullptr;
  String ultimoPagoStr = (ultimoPago != nullptr) ? String(ultimoPago) : "";
  if (!ultimoPagoInicializado) {
    cachedUltimoPagoAt = ultimoPagoStr;
    ultimoPagoInicializado = true;
  } else if (ultimoPagoStr.length() > 0 && ultimoPagoStr != cachedUltimoPagoAt) {
    cachedUltimoPagoAt = ultimoPagoStr;
    pagoAcreditadoPendiente = true;
  }

  // Ver migración 0017 — si falta el campo (placa de cobro con firmware
  // viejo que todavía no lo manda), default false: nunca ocultamos el QR
  // por las dudas, mantiene el comportamiento de siempre.
  cachedIsCleaning = doc["is_cleaning"] | false;

  // Ver migración 0019 — logos subidos desde la app. Si el campo no
  // viene (firmware de pantalla viejo del lado del backend, o campo
  // nunca seteado), default null/0 -> verificarYActualizarLogo() no
  // hace nada y la SD queda exactamente como estaba.
  const char *logoClienteUrl = doc["logo_cliente_url"] | (const char *)nullptr;
  long logoClienteVerNueva = doc["logo_cliente_version"] | (long)0;
  const char *logoNexoUrl = doc["logo_nexo_url"] | (const char *)nullptr;
  long logoNexoVerNueva = doc["logo_nexo_version"] | (long)0;

  if (sdDisponible) {
    verificarYActualizarLogo(RUTA_LOGO_CLIENTE, "/LOGO1VER.TXT", logoClienteVersionCache,
                              logoClienteUrl, logoClienteVerNueva);
    verificarYActualizarLogo(RUTA_LOGO_PROPIO, "/LOGO2VER.TXT", logoNexoVersionCache,
                              logoNexoUrl, logoNexoVerNueva);
  }
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
    return;
  }
  // Limpieza tiene prioridad sobre pausa: aunque alguien reanude la
  // máquina desde el panel justo en medio de un ciclo de limpieza, la
  // placa de cobro sigue limpiando hasta terminar — no tiene sentido
  // mostrar el QR hasta que is_cleaning vuelva a false.
  if (cachedIsCleaning) {
    currentState = SHOW_LIMPIANDO;
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
void abrirPortalDeConfiguracion() {
  currentState = SHOW_CONFIG_PORTAL;
  previousState = SHOW_BOOT;
  dibujarPantallaConfigPortal();
  gfx->flush();

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(100);

  String codigoPrevio = leerCodigoEmparejamiento();
  char codigoPrevioBuf[17];
  codigoPrevio.toCharArray(codigoPrevioBuf, sizeof(codigoPrevioBuf));

  WiFiManagerParameter paramCodigo(
    "pair_code",
    "Codigo de emparejamiento (panel NexoQR)",
    codigoPrevioBuf,
    16
  );
  wm.addParameter(&paramCodigo);

  configurarWatchdog(WDT_TIMEOUT_PORTAL_S);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0);
  wm.startConfigPortal("NexoQR_Display");
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

  gfx->begin();
  gfx->fillScreen(COLOR_MARCA_NAVY);
  gfx->flush();
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  // Touch del "QR de empleado" — bus I2C propio, independiente del QSPI
  // de la pantalla y del SPI de la SD. Si no hay hardware de touch (o
  // está mal cableado), esto no bloquea nada: leerToque() simplemente
  // va a devolver siempre false y la función queda inactiva.
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);

  // Arranque: si la SD esta y tiene el logo propio, lo mostramos a
  // pantalla completa de entrada (eso es lo que se ve mientras conecta
  // WiFi) en vez de carteles de texto con fondo liso. Los carteles de
  // texto quedan sólo como respaldo -- para cuando algo falla y hace
  // falta ver en pantalla qué es (sin SD, sin el archivo del logo, etc).
  sdDisponible = inicializarSD();
  bool bootMuestraLogo = false;

  if (sdDisponible) {
    verificarArchivosLogos();
    cargarVersionesLogosCacheadas(); // NUEVO v2.5.0 — ver descargarLogoASD/verificarYActualizarLogo
    bootMuestraLogo = mostrarLogoPantallaCompleta(RUTA_LOGO_PROPIO);
  }

  if (!sdDisponible) {
    dibujarPantallaBoot(sdEstadoTexto); // "SD no detectada" -- algo real que resolver
    delay(1500);
  } else if (!bootMuestraLogo) {
    dibujarPantallaBoot(logosEstadoTexto); // SD OK pero falta/esta mal algun .BIN
    delay(1500);
  }

  if (!bootMuestraLogo) {
    dibujarPantallaBoot("Iniciando...");
  }

  WiFi.mode(WIFI_STA);
  delay(100);

  logLine("");
  logLine("========================================");
  logLine("NexoQR firmware (display) v" + String(FIRMWARE_VERSION));
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
    if (!bootMuestraLogo) dibujarPantallaBoot("Conectando WiFi...");
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

  if (!bootMuestraLogo) dibujarPantallaBoot("Sincronizando...");
  hacerPoll();
  resolverEstadoVisible();
  previousState = SHOW_BOOT;

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
      lastPollAt = 0;
    }
  } else {
    botonChequeado = false;
  }

  // Touch del "QR de empleado" — detección de flanco (toque nuevo) con
  // un pequeño debounce, para no disparar manejarToque() en cada vuelta
  // del loop mientras el dedo sigue apoyado.
  {
    int touchX = 0, touchY = 0;
    bool hayToqueAhora = leerToque(touchX, touchY);
    bool toqueNuevo = hayToqueAhora && !touchTocadoAntes &&
                       (millis() - ultimoToqueRegistradoAt > DEBOUNCE_TOQUE_MS);
    touchTocadoAntes = hayToqueAhora;
    if (toqueNuevo) {
      ultimoToqueRegistradoAt = millis();
      manejarToque(touchX, touchY);
    }
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
      Serial.println("microSD: " + sdEstadoTexto);
      Serial.println("Paso slideshow actual: " + String((int)pasoActual));
      Serial.println("QR empleado configurado: " + String(cachedEmpleadoPin.length() > 0 ? "si" : "no"));
    } else if (key == 't' || key == 'T') {
      int tx = 0, ty = 0;
      bool ok = leerToque(tx, ty);
      Serial.println(ok ? ("Touch: x=" + String(tx) + " y=" + String(ty)) : "Touch: sin toque ahora mismo.");
    }
  }

  delay(10);
}
