// =====================================================================
//  PRUEBA DEL KIT SIN CABLE USB — Equipo ARCE
//
//  Cómo leer los resultados sin monitor serie:
//   1) LED de la placa (GPIO16): parpadea N veces cada 4 s
//        N = módulos encontrados (XN03, XN13, XN44 -> máximo 3)
//        parpadeo rápido continuo = NO encontró ningún módulo
//   2) Buzzer XN44: al arrancar suena 1 pitido por cada módulo encontrado
//   3) LED RGB del XN03 (botón BOOT cambia de modo, suena 1/2/3 pitidos):
//        Modo 1: MORADO fijo
//        Modo 2: color del pH medido por la sonda (indicador universal)
//        Modo 3: color según el potenciómetro (gíralo: rojo -> verde -> violeta)
//   4) Wi-Fi: conecta el celular a la red  "ARCE-prueba"  (clave: arce2026)
//        y abre  http://192.168.4.1  -> pH, voltaje, ADC, potenciómetro y módulos
//
//  El relevador (bomba) NO se activa en esta prueba: solo se comprueba
//  que responde, escribiendo "apagado" y leyéndolo de vuelta.
// =====================================================================
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

#define I2C_SDA      12
#define I2C_SCL      13
#define BOARD_LED    16
#define BOARD_BUTTON 0

const uint8_t ADDR_XN03 = 0x03;   // E/S analógicas: sonda pH en IN1, pot, LED RGB
const uint8_t ADDR_XN13 = 0x0D;   // relevador de la bomba
const uint8_t ADDR_XN44 = 0x2C;   // buzzer
const uint8_t LED_MAX   = 100;    // subir a 255 si el RGB se ve tenue

const char* AP_SSID = "ARCE-prueba";
const char* AP_PASS = "arce2026";

WebServer server(80);

bool hay03 = false, hay13 = false, hay44 = false, releOk = false, apOk = false;
uint8_t nModulos = 0;
uint8_t modo = 0;                 // 0 morado, 1 pH, 2 potenciómetro
uint16_t adcPh = 0;
float voltPh = NAN, ph = NAN;
int pot = -1;
uint8_t colR = 0, colG = 0, colB = 0;

// ---------------- I2C ----------------
bool presente(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
bool xnWrite(uint8_t a, uint8_t r, const uint8_t* d, size_t n) {
  Wire.beginTransmission(a); Wire.write(r); Wire.write(d, n);
  return Wire.endTransmission() == 0;
}
bool xnRead(uint8_t a, uint8_t r, uint8_t* b, size_t n) {
  Wire.beginTransmission(a); Wire.write(r);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(a, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) b[i] = Wire.read();
  return true;
}
bool xnRead16(uint8_t a, uint8_t r, uint16_t& o) {
  uint8_t b[2];
  if (!xnRead(a, r, b, 2)) return false;
  o = (uint16_t(b[0]) << 8) | b[1];
  return true;
}

// ---------------- Actuadores ----------------
void tono(uint16_t hz, uint16_t ms) {
  if (!hay44) return;
  uint8_t d[4] = { uint8_t(hz >> 8), uint8_t(hz), uint8_t(ms >> 8), uint8_t(ms) };
  xnWrite(ADDR_XN44, 0x01, d, 4);
}
void pitidos(uint8_t n, uint16_t hz) {
  for (uint8_t i = 0; i < n; i++) { tono(hz, 120); delay(250); }
}
void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  if (!hay03) return;
  uint8_t d[3] = { uint8_t(r * LED_MAX / 255), uint8_t(g * LED_MAX / 255), uint8_t(b * LED_MAX / 255) };
  xnWrite(ADDR_XN03, 0x01, d, 3);
}

// ---------------- Indicador universal ----------------
const uint8_t COLOR_PH[15][3] = {
  {255, 0, 0},   {230, 20, 20}, {255, 40, 0},  {255, 90, 0},  {255, 140, 0},
  {255, 200, 0}, {255, 255, 0}, {0, 200, 0},   {0, 180, 120}, {0, 90, 255},
  {0, 0, 180},   {75, 0, 130},  {140, 0, 200}, {148, 0, 211}, {90, 0, 140}
};
void colorDePh(float p) {
  if (isnan(p)) { colR = colG = colB = 0; return; }
  p = constrain(p, 0.0f, 14.0f);
  uint8_t i = (uint8_t)floorf(p); if (i >= 14) i = 13;
  float t = p - i;
  colR = COLOR_PH[i][0] + t * (COLOR_PH[i + 1][0] - COLOR_PH[i][0]);
  colG = COLOR_PH[i][1] + t * (COLOR_PH[i + 1][1] - COLOR_PH[i][1]);
  colB = COLOR_PH[i][2] + t * (COLOR_PH[i + 1][2] - COLOR_PH[i][2]);
}

// ---------------- Pruebas ----------------
void detectar() {
  hay03 = presente(ADDR_XN03);
  hay13 = presente(ADDR_XN13);
  hay44 = presente(ADDR_XN44);
  nModulos = hay03 + hay13 + hay44;
  releOk = false;
  if (hay13) {                                    // escribe "apagado" y lo lee de vuelta
    uint8_t off = 0x00, leido = 0xFF;
    releOk = xnWrite(ADDR_XN13, 0x01, &off, 1) && xnRead(ADDR_XN13, 0x01, &leido, 1) && leido == 0x00;
  }
}

void medir() {
  if (!hay03) { ph = NAN; voltPh = NAN; pot = -1; return; }
  uint32_t suma = 0; uint8_t n = 0; uint16_t raw;
  for (uint8_t i = 0; i < 16; i++) {              // promedio de 16 lecturas
    if (xnRead16(ADDR_XN03, 0x21, raw) && raw <= 1023) { suma += raw; n++; }
  }
  if (n) {
    adcPh = suma / n;
    voltPh = adcPh * 3.3f / 1023.0f;
    ph = constrain(7.0f + (adcPh - 511.0f) * (-3.0f) / (674.0f - 511.0f), 0.0f, 14.0f);
  } else { ph = NAN; voltPh = NAN; }
  uint8_t p;
  pot = xnRead(ADDR_XN03, 0x10, &p, 1) ? p : -1;
}

void actualizarColor() {
  if (modo == 0)      { colR = 128; colG = 0; colB = 255; }          // morado
  else if (modo == 1) { colorDePh(ph); }
  else                { colorDePh(pot < 0 ? NAN : pot * 14.0f / 100.0f); }
  setRGB(colR, colG, colB);
}

// ---------------- Página web ----------------
const char* NOMBRE_MODO[] = { "Morado fijo", "Color del pH (sonda)", "Color del potenciometro" };
String si(bool b) { return b ? "<b style='color:#1a7f37'>SI</b>" : "<b style='color:#cf222e'>NO</b>"; }

void paginaInicio() {
  char hex[8]; snprintf(hex, sizeof(hex), "#%02X%02X%02X", colR, colG, colB);
  String h = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<meta http-equiv='refresh' content='2'><title>Prueba kit ARCE</title>"
             "<style>body{font-family:sans-serif;margin:16px;background:#f6f8fa;color:#1f2328}"
             "table{border-collapse:collapse;width:100%;max-width:480px;background:#fff}"
             "td{border:1px solid #d0d7de;padding:8px}a{display:inline-block;margin:4px;padding:10px 14px;"
             "background:#0969da;color:#fff;border-radius:6px;text-decoration:none}</style></head><body>";
  h += "<h2>Prueba del kit — ARCE</h2>";
  h += "<div style='width:100%;max-width:480px;height:48px;border-radius:8px;background:" + String(hex) + "'></div><br>";
  h += "<table>";
  h += "<tr><td>XN03 (pH / pot / RGB) en 0x03</td><td>" + si(hay03) + "</td></tr>";
  h += "<tr><td>XN13 (relevador) en 0x0D</td><td>" + si(hay13) + (hay13 ? (releOk ? " (responde)" : " (no confirma)") : "") + "</td></tr>";
  h += "<tr><td>XN44 (buzzer) en 0x2C</td><td>" + si(hay44) + "</td></tr>";
  h += "<tr><td>pH (calibración de fábrica)</td><td><b>" + (isnan(ph) ? String("--") : String(ph, 2)) + "</b></td></tr>";
  h += "<tr><td>Voltaje sonda (IN1)</td><td>" + (isnan(voltPh) ? String("--") : String(voltPh, 3) + " V") + "</td></tr>";
  h += "<tr><td>ADC sonda (0-1023)</td><td>" + String(adcPh) + " <small>(pH 7 ≈ 511, pH 4 ≈ 674)</small></td></tr>";
  h += "<tr><td>Potenciómetro (0-100)</td><td>" + (pot < 0 ? String("--") : String(pot)) + "</td></tr>";
  h += "<tr><td>Modo LED</td><td>" + String(NOMBRE_MODO[modo]) + "</td></tr>";
  h += "<tr><td>Tiempo encendido</td><td>" + String(millis() / 1000) + " s</td></tr>";
  h += "</table><p><a href='/modo?m=0'>Morado</a><a href='/modo?m=1'>pH</a><a href='/modo?m=2'>Pot</a><a href='/beep'>Probar buzzer</a></p>";
  h += "</body></html>";
  server.send(200, "text/html; charset=utf-8", h);
}
void paginaModo() {
  if (server.hasArg("m")) { modo = constrain(server.arg("m").toInt(), 0, 2); actualizarColor(); }
  server.sendHeader("Location", "/"); server.send(303);
}
void paginaBeep() {
  tono(1320, 200);
  server.sendHeader("Location", "/"); server.send(303);
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  pinMode(BOARD_LED, OUTPUT);
  pinMode(BOARD_BUTTON, INPUT_PULLUP);
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin();
  delay(300);                                     // los X-NODE tardan un poco en arrancar

  detectar();
  if (hay44) pitidos(nModulos, 1000);             // 1 pitido por módulo encontrado
  medir();
  actualizarColor();

  WiFi.mode(WIFI_AP);
  apOk = WiFi.softAP(AP_SSID, AP_PASS, 6);       // canal 6 (2.4 GHz)
  if (hay44) { if (apOk) tono(2000, 400); else pitidos(5, 300); }   // tono agudo largo = Wi-Fi OK
  server.on("/", paginaInicio);
  server.on("/modo", paginaModo);
  server.on("/beep", paginaBeep);
  server.begin();
}

void loop() {
  uint32_t ahora = millis();
  server.handleClient();

  static uint32_t tDetectar = 0, tMedir = 0, tLog = 0;
  if (ahora - tDetectar >= 3000) {                // vuelve a buscar módulos (por si se conectan después)
    tDetectar = ahora;
    uint8_t antes = nModulos;
    detectar();
    if (nModulos > antes) pitidos(nModulos - antes, 1500);
  }
  if (ahora - tMedir >= 300) { tMedir = ahora; medir(); actualizarColor(); }

  // LED de placa: N parpadeos cada 4 s (N = módulos); rápido continuo si no hay ninguno
  bool led;
  if (nModulos == 0) led = (ahora % 200) < 100;
  else { uint32_t f = ahora % 4000; led = (f / 500) < nModulos && (f % 500) < 200; }
  digitalWrite(BOARD_LED, led);

  // Botón BOOT: cambia de modo
  static bool btnPrev = HIGH; static uint32_t tBtn = 0;
  bool btn = digitalRead(BOARD_BUTTON);
  if (btn != btnPrev && ahora - tBtn > 50) {
    tBtn = ahora; btnPrev = btn;
    if (btn == LOW) { modo = (modo + 1) % 3; actualizarColor(); pitidos(modo + 1, 1760); }
  }

  if (ahora - tLog >= 1000) {                     // por si hay monitor serie conectado
    tLog = ahora;
    Serial.printf("XN03=%d XN13=%d(%d) XN44=%d | adc=%u V=%.3f pH=%.2f pot=%d modo=%u | WiFi AP %s %s clientes=%d\n",
                  hay03, hay13, releOk, hay44, adcPh, voltPh, ph, pot, modo,
                  apOk ? "OK" : "FALLO", WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum());
  }
}
