// =====================================================================
//  PRUEBA DEL KIT SIN CABLE USB — Equipo ARCE
//
//  Cómo leer los resultados sin monitor serie:
//   1) LED de la placa (GPIO16): parpadea N veces cada 4 s
//        N = módulos encontrados (XN03, XN13, XN44 -> máximo 3)
//        parpadeo rápido continuo = NO encontró ningún módulo
//   2) Buzzer XN44: al arrancar suena 1 pitido por cada módulo encontrado
//   3) LED RGB del XN03 (botón BOOT cambia de modo, suena 1/2/3 pitidos):
//        Modo 1: MORADO fijo  (BOOT para elegirlo)
//        Modo 2: color del pH medido por la sonda (indicador universal) ← AL ARRANCAR
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
#include <ArduinoOTA.h>
#include <Update.h>
#include <math.h>

#define I2C_SDA      12
#define I2C_SCL      13
#define BOARD_LED    16
#define BOARD_BUTTON 0

const uint8_t ADDR_XN03 = 0x03;   // E/S analógicas: sonda pH en IN1, pot, LED RGB
const uint8_t ADDR_XN13 = 0x0D;   // relevador de la bomba
const uint8_t ADDR_XN44 = 0x2C;   // buzzer
const uint8_t LED_MAX   = 255;    // el XN03 acepta 0–255 (verificado)
// Calibración ARCE 19-nov: ADC 607 = pH 6.95 (pendiente de fábrica 54.33 ADC/pH)
const float CAL_ADC7 = 604.3f, CAL_ADC4 = 767.3f;

const char* AP_SSID = "ARCE-prueba";
const char* AP_PASS = "arce2026";

WebServer server(80);

bool hay03 = false, hay13 = false, hay44 = false, releOk = false, apOk = false;
uint8_t nModulos = 0;
uint8_t modo = 1;                 // 0 morado, 1 pH (por defecto), 2 potenciómetro
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
  {255, 0, 0},   {230, 20, 20}, {220, 10, 10}, {255, 90, 0},  {255, 140, 0},
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
// Identidad de cada módulo: índice 0 = XN03, 1 = XN13, 2 = XN44
const uint8_t DIRS[3] = { ADDR_XN03, ADDR_XN13, ADDR_XN44 };
uint8_t whoAmI[3][2], firmware[3][3], estadoMod[3];
bool infoOk[3] = { false, false, false };
String busEncontrados = "";                       // todas las direcciones que responden
uint32_t lecturasOk = 0, lecturasFallo = 0;       // fiabilidad del bus
uint16_t adcIn2 = 0;                              // salida To (temperatura) de la sonda

// Lectura con "repeated start" (sin STOP entre escribir el registro y leer).
// Se prueba en el XN13 porque con la lectura normal devuelve siempre 0xFF.
bool xnReadRS(uint8_t a, uint8_t r, uint8_t* b, size_t n) {
  Wire.beginTransmission(a); Wire.write(r);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(a, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) b[i] = Wire.read();
  return true;
}
uint8_t who13rs[2] = {0, 0}, fw13rs[3] = {0, 0, 0}, rele13rs = 0xEE;
bool rs13Ok = false;

void leerIdentidad(uint8_t i) {
  infoOk[i] = xnRead(DIRS[i], 0x3E, whoAmI[i], 2) &&
              xnRead(DIRS[i], 0x38, firmware[i], 3) &&
              xnRead(DIRS[i], 0x37, &estadoMod[i], 1);
}

// XN13 con firmware 0.1: se apaga por UART (XN13A+S=0 -> "OK"). Por I2C, 01,00 podía ENCENDERLO.
bool uartListo = false;
bool releApagarUart() {
  if (!uartListo) { Serial2.begin(115200, SERIAL_8N1, 9, 10); uartListo = true; delay(10); }
  while (Serial2.available()) Serial2.read();
  Serial2.print("XN13A+S=0\r\n");
  String r; uint32_t t0 = millis();
  while (millis() - t0 < 80) { while (Serial2.available()) r += (char)Serial2.read(); if (r.endsWith("\n")) break; delay(1); }
  r.trim(); return r == "OK";
}

void detectar() {
  hay03 = presente(ADDR_XN03);
  hay13 = presente(ADDR_XN13);
  hay44 = presente(ADDR_XN44);
  nModulos = hay03 + hay13 + hay44;
  bool hay[3] = { hay03, hay13, hay44 };
  for (uint8_t i = 0; i < 3; i++) { if (hay[i]) leerIdentidad(i); else infoOk[i] = false; }

  busEncontrados = "";                            // barrido completo 0x01-0x7E
  for (uint8_t a = 1; a < 0x7F; a++) {
    if (presente(a)) { char b[6]; snprintf(b, sizeof(b), "0x%02X ", a); busEncontrados += b; }
  }

  releOk = false;
  if (hay13) {                                    // NUNCA enciende: escribe "apagado" y lo lee de vuelta
    uint8_t off = 0x00, leido = 0xFF;
    (void)off; (void)leido;
    releOk = releApagarUart();                    // confirma "OK" por UART
    rs13Ok = xnReadRS(ADDR_XN13, 0x3E, who13rs, 2) && xnReadRS(ADDR_XN13, 0x38, fw13rs, 3) &&
             xnReadRS(ADDR_XN13, 0x01, &rele13rs, 1);
  }
}

void medir() {
  if (!hay03) { ph = NAN; voltPh = NAN; pot = -1; return; }
  uint32_t suma = 0; uint8_t n = 0; uint16_t raw;
  for (uint8_t i = 0; i < 16; i++) {              // promedio de 16 lecturas
    if (xnRead16(ADDR_XN03, 0x21, raw) && raw <= 1023) { suma += raw; n++; lecturasOk++; }
    else lecturasFallo++;
  }
  if (xnRead16(ADDR_XN03, 0x22, raw) && raw <= 1023) adcIn2 = raw;
  if (n) {
    adcPh = suma / n;
    voltPh = adcPh * 3.3f / 1023.0f;
    ph = constrain(7.0f + (adcPh - CAL_ADC7) * (-3.0f) / (CAL_ADC4 - CAL_ADC7), 0.0f, 14.0f);
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
  h += "<tr><td>pH (calibrado 607 = 6.95)</td><td><b>" + (isnan(ph) ? String("--") : String(ph, 2)) + "</b></td></tr>";
  h += "<tr><td>Voltaje sonda (IN1)</td><td>" + (isnan(voltPh) ? String("--") : String(voltPh, 3) + " V") + "</td></tr>";
  h += "<tr><td>ADC sonda (0-1023)</td><td>" + String(adcPh) + " <small>(calibrado: 607 = pH 6.95)</small></td></tr>";
  h += "<tr><td>Potenciómetro (0-100)</td><td>" + (pot < 0 ? String("--") : String(pot)) + "</td></tr>";
  h += "<tr><td>ADC IN2 (To, temperatura)</td><td>" + String(adcIn2) + " (" + String(adcIn2 * 3.3f / 1023.0f, 3) + " V)</td></tr>";
  const char* nom[3] = { "XN03", "XN13", "XN44" };
  for (uint8_t i = 0; i < 3; i++) {
    h += "<tr><td>" + String(nom[i]) + " WHO_AM_I / FW / STAT</td><td>";
    if (infoOk[i]) {
      char b[48];
      snprintf(b, sizeof(b), "modelo %u rev %u / %u.%u.%u / 0x%02X", whoAmI[i][0], whoAmI[i][1],
               firmware[i][0], firmware[i][1], firmware[i][2], estadoMod[i]);
      h += b;
    } else h += "--";
    h += "</td></tr>";
  }
  {
    char b[80];
    if (rs13Ok) snprintf(b, sizeof(b), "modelo %u rev %u / %u.%u.%u / RELAY=0x%02X", who13rs[0], who13rs[1],
                         fw13rs[0], fw13rs[1], fw13rs[2], rele13rs);
    else snprintf(b, sizeof(b), "--");
    h += "<tr><td>XN13 leído con repeated start</td><td>" + String(b) + "</td></tr>";
  }
  h += "<tr><td>Bus I2C (todas las direcciones)</td><td>" + (busEncontrados.length() ? busEncontrados : String("ninguna")) + "</td></tr>";
  uint32_t tot = lecturasOk + lecturasFallo;
  h += "<tr><td>Lecturas I2C correctas</td><td>" + String(lecturasOk) + " / " + String(tot) +
       (tot ? " (" + String(100.0f * lecturasOk / tot, 1) + " %)" : String("")) + "</td></tr>";
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
// ---------------- Actualización por HTTP (/update) ----------------
// Desde el navegador: http://192.168.4.1/update (usuario: arce, clave: arce2026)
// Desde la terminal:  curl -u arce:arce2026 -F "firmware=@prueba_kit.ino.bin" http://192.168.4.1/update
const char* OTA_USER = "arce";
bool otaError = false;

void paginaUpdateForm() {
  if (!server.authenticate(OTA_USER, AP_PASS)) return server.requestAuthentication();
  server.send(200, "text/html; charset=utf-8",
    "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<body style='font-family:sans-serif;margin:16px'><h2>Actualizar firmware</h2>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'><br><br><input type='submit' value='Subir'></form></body>");
}
void paginaUpdateFin() {
  if (!server.authenticate(OTA_USER, AP_PASS)) return server.requestAuthentication();
  bool ok = !otaError && !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK, reiniciando\n" : "ERROR en la actualizacion\n");
  delay(300);
  if (ok) ESP.restart();
}
void paginaUpdateDatos() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaError = !server.authenticate(OTA_USER, AP_PASS);
    if (otaError) return;
    releApagarUart();                             // relevador apagado durante la actualización
    setRGB(0, 0, 255);
    Serial.printf("OTA HTTP: recibiendo %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) otaError = true;
  } else if (up.status == UPLOAD_FILE_WRITE && !otaError) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) otaError = true;
  } else if (up.status == UPLOAD_FILE_END && !otaError) {
    if (!Update.end(true)) otaError = true;
    Serial.printf("OTA HTTP: %u bytes, %s\n", up.totalSize, otaError ? "ERROR" : "OK");
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort(); otaError = true;
  }
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
  server.on("/update", HTTP_GET, paginaUpdateForm);
  server.on("/update", HTTP_POST, paginaUpdateFin, paginaUpdateDatos);
  server.begin();

  // Carga de programas por Wi-Fi (OTA): conectar el portátil a "ARCE-prueba" y subir a 192.168.4.1
  ArduinoOTA.setHostname("arce-ph");
  ArduinoOTA.setPassword(AP_PASS);
  ArduinoOTA.onStart([]() {
    releApagarUart();                             // relevador apagado durante la actualización
    setRGB(0, 0, 255);
  });
  ArduinoOTA.begin();
}

void loop() {
  uint32_t ahora = millis();
  server.handleClient();
  ArduinoOTA.handle();

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
    Serial.printf("XN03=%d XN13=%d(%d) XN44=%d | bus: %s| adc1=%u V=%.3f pH=%.2f adc2=%u pot=%d | i2c ok=%lu fallo=%lu | modo=%u | WiFi AP %s %s clientes=%d\n",
                  hay03, hay13, releOk, hay44, busEncontrados.c_str(), adcPh, voltPh, ph, adcIn2, pot,
                  (unsigned long)lecturasOk, (unsigned long)lecturasFallo, modo,
                  apOk ? "OK" : "FALLO", WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum());
    if (hay13) Serial.printf("  XN13 repeated start: ok=%d WHO_AM_I=%u.%u FW=%u.%u.%u RELAY=0x%02X\n", rs13Ok,
                             who13rs[0], who13rs[1], fw13rs[0], fw13rs[1], fw13rs[2], rele13rs);
    for (uint8_t i = 0; i < 3; i++) if (infoOk[i])
      Serial.printf("  0x%02X WHO_AM_I=%u.%u FW=%u.%u.%u STAT=0x%02X\n", DIRS[i], whoAmI[i][0], whoAmI[i][1],
                    firmware[i][0], firmware[i][1], firmware[i][2], estadoMod[i]);
  }
}
