// =====================================================================
//  Reto: Monitoreo y control de calidad de agua IoT — Equipo ARCE
//  Hackatón Puebla 2026 · Kit XIDE
//
//  Conexiones (diagramas del reto):
//    Sonda pH  Po -> XN03 IN1 (reg ADC1 0x21, 10 bits)   To -> XN03 IN2 (0x22)
//              V+ -> XN03 3V3, G -> XN03 GND
//    Bomba 12V: Fuente(+) -> XN13 COM, XN13 NA -> Bomba(+), Bomba(-) -> Fuente(-)
//    XN14: fuente 12 V a GND(-) / 12V(+)
//    XN44: buzzer (0x2C) · XN03 LED RGB = indicador universal
//
//  Algoritmo de dosificación: "pulsa y espera" adaptativo
//    1. Pulso corto de bomba (mín. 50 ms ≈ 1.65 mL a 33 mL/s).
//    2. Espera a que la solución se propague y la lectura se estabilice.
//    3. Mide cuánto bajó el pH -> estima la ganancia (pH por ms de bomba).
//    4. Siguiente pulso = FRACCION * error / ganancia (nunca apunta a pasarse).
//    5. Termina cuando pH <= umbral + TOLERANCIA, o cuando incluso el pulso
//       mínimo se pasaría del umbral.
//
//  Comandos por monitor serie (115200, terminar con Enter):
//    start | stop | umbral 5.5 | cal7 | cal4 | calreset | cebar 300 | info
//
//  Para Blynk: copia secrets.h.example a secrets.h y descomenta USE_BLYNK.
// =====================================================================
#define USE_BLYNK

#ifdef USE_BLYNK
#include "secrets.h"
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#endif
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <math.h>
#include "web_ui.h"                 // dashboard propio (HTML/CSS/JS inline, sin Internet)

// Red propia del ESP32 (siempre activa, aunque no haya Internet):
//   estado en http://192.168.4.1  ·  actualizar firmware en http://192.168.4.1/update (arce / arce2026)
const char* AP_SSID  = "ARCE-prueba";
const char* AP_PASS  = "arce2026";
const char* OTA_USER = "arce";

// ---------------- Hardware ----------------
#define I2C_SDA      12
#define I2C_SCL      13
#define BOARD_LED    16
#define BOARD_BUTTON 0

const uint8_t ADDR_XN03 = 0x03;
const uint8_t ADDR_XN13 = 0x0D;
const uint8_t ADDR_XN44 = 0x2C;
const uint8_t REG_ADC_PH   = 0x21;   // XN03 ADC1 (IN1)
const uint8_t REG_ADC_TEMP = 0x22;   // XN03 ADC2 (IN2)
const uint8_t REG_RGB      = 0x01;   // XN03 O1_RED.. (R, G, B)
const uint8_t REG_RELAY    = 0x01;   // XN13
const uint8_t REG_TONE     = 0x01;   // XN44
const uint8_t LED_MAX      = 100;    // subir a 255 si el RGB del XN03 usa 0–255

// ---------------- Calibración pH ----------------
// Tabla del fabricante (ADC 10 bits, 3.3 V): pH 7 = 511, pH 4 = 674 (pendiente 54.33 ADC/pH).
// Calibración ARCE 19-nov: ADC 607 = pH 6.95 (1 punto, pendiente de fábrica) -> pH 7 = 604.3, pH 4 = 767.3
const float CAL_ADC7_DEF = 604.3f, CAL_ADC4_DEF = 767.3f;
// Recalibrar en sitio con "cal7" (agua ≈ 7) y "cal4" (solución pH 4).
float calAdc7 = CAL_ADC7_DEF;
float calAdc4 = CAL_ADC4_DEF;

// ---------------- Parámetros de control ----------------
float umbral = 6.0f;
const float UMBRAL_MIN = 4.5f, UMBRAL_MAX = 7.0f;
const float TOLERANCIA          = 0.05f;   // pH: "alcanzado" si pH <= umbral + TOLERANCIA
const float FRACCION            = 0.6f;    // fracción del error que se corrige por pulso
const uint32_t PULSO_MIN_MS     = 50;      // límite del relevador
const uint32_t PULSO_INICIAL_MS = 100;     // primer pulso, ganancia aún desconocida
const uint32_t PULSO_MAX_MS     = 600;
const uint32_t ESPERA_MIN_MS    = 6000;    // tiempo mínimo de propagación
const uint32_t ESPERA_MAX_MS    = 30000;
const float PENDIENTE_ESTABLE   = 0.01f;   // pH/s para considerar la lectura estable
const float CAUDAL_ML_S         = 33.0f;
const uint32_t BOMBA_TOTAL_MAX_MS = 2500;  // ≈ 82 mL (hay 100 mL de solución)
const uint8_t MAX_PULSOS_SIN_EFECTO = 5;   // incluye el cebado de la manguera
const float CAMBIO_MINIMO       = 0.02f;   // pH: por debajo, el pulso "no tuvo efecto"

// ---------------- Helpers I2C ----------------
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

// ---------------- Bomba ----------------
bool bombaOn = false;
uint32_t bombaApagarEn = 0, bombaInicio = 0;
uint32_t bombaTotalMs = 0;            // acumulado de la sesión de dosificación
uint32_t bombaHistoricoMs = 0;        // acumulado desde el arranque (consumo de solución)

bool setRele(bool on) {
  uint8_t v = on ? 0x01 : 0x00;
  return xnWrite(ADDR_XN13, REG_RELAY, &v, 1);
}
void bombaPulso(uint32_t ms) {
  if (!setRele(true)) { Serial.println("# ERROR: no responde el relevador XN13"); return; }
  bombaOn = true;
  bombaInicio = millis();
  bombaApagarEn = bombaInicio + ms;
  digitalWrite(BOARD_LED, HIGH);
}
bool releApagadoPendiente = false;   // si falla el I2C al apagar, se reintenta en cada loop
void bombaApagar() {
  if (bombaOn) {
    uint32_t real = millis() - bombaInicio;
    bombaTotalMs += real;
    bombaHistoricoMs += real;
    bombaOn = false;
  }
  releApagadoPendiente = !setRele(false);
  if (releApagadoPendiente) Serial.println("# ERROR: no se pudo apagar el relevador, reintentando");
  digitalWrite(BOARD_LED, LOW);
}

// ---------------- Buzzer (melodías no bloqueantes) ----------------
const uint16_t MEL_INICIO[][2]  = { {880, 120}, {1320, 120}, {1760, 220} };   // ascendente
const uint16_t MEL_FIN[][2]     = { {1760, 120}, {1320, 120}, {880, 300} };   // descendente
const uint16_t MEL_RECHAZO[][2] = { {300, 200}, {0, 100}, {300, 200} };       // grave doble
const uint16_t (*melodia)[2] = nullptr;
uint8_t melLen = 0, melIdx = 0;
uint32_t melSiguiente = 0;

void tono(uint16_t hz, uint16_t ms) {
  uint8_t d[4] = { uint8_t(hz >> 8), uint8_t(hz), uint8_t(ms >> 8), uint8_t(ms) };
  xnWrite(ADDR_XN44, REG_TONE, d, 4);
}
void tocar(uint8_t cual) {   // 0 inicio, 1 fin, 2 rechazo
  if (cual == 0)      { melodia = MEL_INICIO;  melLen = 3; }
  else if (cual == 1) { melodia = MEL_FIN;     melLen = 3; }
  else                { melodia = MEL_RECHAZO; melLen = 3; }
  melIdx = 0; melSiguiente = millis();
}
void actualizarMelodia() {
  if (!melodia || melIdx >= melLen || (int32_t)(millis() - melSiguiente) < 0) return;
  uint16_t hz = melodia[melIdx][0], ms = melodia[melIdx][1];
  if (hz) tono(hz, ms);
  melSiguiente = millis() + ms + 30;
  if (++melIdx >= melLen) melodia = nullptr;
}

// ---------------- Lectura de pH con filtro ----------------
const uint8_t N_MUESTRAS = 25;          // 25 × 20 ms = 0.5 s de ventana
uint16_t muestras[N_MUESTRAS];
uint8_t nMuestras = 0, idxMuestra = 0;
uint8_t fallosLectura = 0;
float adcFiltrado = NAN, ph = NAN, voltaje = NAN, voltTemp = NAN;
float historialPh[10];                  // 1 muestra por segundo
uint8_t nHist = 0, idxHist = 0;

float adcAPh(float adc) { return 7.0f + (adc - calAdc7) * (4.0f - 7.0f) / (calAdc4 - calAdc7); }

// Media recortada: ordena la ventana y promedia el 50 % central (elimina picos de ruido)
float mediaRecortada() {
  uint16_t tmp[N_MUESTRAS];
  memcpy(tmp, muestras, nMuestras * sizeof(uint16_t));
  for (uint8_t i = 1; i < nMuestras; i++) {           // inserción (n pequeño)
    uint16_t k = tmp[i]; int8_t j = i - 1;
    while (j >= 0 && tmp[j] > k) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  uint8_t ini = nMuestras / 4, fin = nMuestras - nMuestras / 4;
  uint32_t suma = 0;
  for (uint8_t i = ini; i < fin; i++) suma += tmp[i];
  return float(suma) / (fin - ini);
}

void muestrear() {
  uint16_t raw;
  if (xnRead16(ADDR_XN03, REG_ADC_PH, raw) && raw <= 1023) {
    muestras[idxMuestra] = raw;
    idxMuestra = (idxMuestra + 1) % N_MUESTRAS;
    if (nMuestras < N_MUESTRAS) nMuestras++;
    fallosLectura = 0;
  } else if (fallosLectura < 255) {
    fallosLectura++;
  }
}

void calcularPh() {
  if (fallosLectura > 10 || nMuestras < 5) { ph = NAN; return; }
  float m = mediaRecortada();
  adcFiltrado = isnan(adcFiltrado) ? m : 0.7f * adcFiltrado + 0.3f * m;   // suavizado EMA
  voltaje = adcFiltrado * 3.3f / 1023.0f;
  ph = constrain(adcAPh(adcFiltrado), 0.0f, 14.0f);
  uint16_t rt;
  if (xnRead16(ADDR_XN03, REG_ADC_TEMP, rt) && rt <= 1023) voltTemp = rt * 3.3f / 1023.0f;
}

void guardarHistorial() {
  if (isnan(ph)) return;
  historialPh[idxHist] = ph;
  idxHist = (idxHist + 1) % 10;
  if (nHist < 10) nHist++;
}
// pH/s en los últimos ~4 s (positivo = subiendo)
float pendientePh() {
  if (nHist < 5) return NAN;
  float ahora = historialPh[(idxHist + 9) % 10];
  float antes = historialPh[(idxHist + 5) % 10];
  return (ahora - antes) / 4.0f;
}

// ---------------- Indicador universal (tabla del reto) ----------------
//  0 rojo intenso · 1-2 rojo · 3 rojo-naranja · 4 naranja · 5 amarillo-naranja · 6 amarillo
//  7 verde · 8 verde azulado · 9 azul · 10 azul oscuro · 11 índigo · 12-13 violeta · 14 violeta oscuro
const uint8_t COLOR_PH[15][3] = {
  {255, 0, 0},   {230, 20, 20}, {220, 10, 10}, {255, 90, 0},  {255, 140, 0},
  {255, 200, 0}, {255, 255, 0}, {0, 200, 0},   {0, 180, 120}, {0, 90, 255},
  {0, 0, 180},   {75, 0, 130},  {140, 0, 200}, {148, 0, 211}, {90, 0, 140}
};
uint8_t colR = 0, colG = 0, colB = 0;
void colorDePh(float p) {
  if (isnan(p)) { colR = colG = colB = 0; return; }
  p = constrain(p, 0.0f, 14.0f);
  uint8_t i = (uint8_t)floorf(p); if (i >= 14) i = 13;
  float t = p - i;
  colR = COLOR_PH[i][0] + t * (COLOR_PH[i + 1][0] - COLOR_PH[i][0]);
  colG = COLOR_PH[i][1] + t * (COLOR_PH[i + 1][1] - COLOR_PH[i][1]);
  colB = COLOR_PH[i][2] + t * (COLOR_PH[i + 1][2] - COLOR_PH[i][2]);
}
void actualizarLed() {
  uint8_t d[3] = { uint8_t(colR * LED_MAX / 255), uint8_t(colG * LED_MAX / 255), uint8_t(colB * LED_MAX / 255) };
  xnWrite(ADDR_XN03, REG_RGB, d, 3);
}

// ---------------- Máquina de estados de dosificación ----------------
// 0 REPOSO · 1 PULSO (bomba encendida) · 2 MEZCLA (esperando propagación)
uint8_t estado = 0;
const char* NOMBRE_ESTADO[] = { "REPOSO", "DOSIFICANDO", "MEZCLANDO" };
String ultimoMensaje = "Listo";
float ganancia = 0;            // pH que baja por ms de bomba (0 = desconocida)
float phAntesPulso = NAN;
uint32_t pulsoActualMs = 0, inicioMezcla = 0;
uint8_t pulsosSinEfecto = 0, nPulsos = 0;

Preferences prefs;
void guardarPrefs() {
  prefs.putFloat("cal7", calAdc7); prefs.putFloat("cal4", calAdc4); prefs.putFloat("umbral", umbral);
}

void notificar(const String& msg) {
  ultimoMensaje = msg;
  Serial.println("# " + msg);
}
#ifdef USE_BLYNK
void blynkEvento(const char* codigo, const String& msg) { if (Blynk.connected()) Blynk.logEvent(codigo, msg); }
#else
void blynkEvento(const char*, const String&) {}
#endif

// Métricas de la última dosificación (para el dashboard local y Blynk V8)
bool sesValida = false, sesExito = false;
float sesPhIni = NAN, sesPhFin = NAN, sesUmbral = NAN, sesError = NAN, sesMl = 0;
uint8_t sesPulsos = 0;
uint32_t sesInicioMs = 0, sesDuracionS = 0;
String sesMotivo = "";

void finalizar(const String& motivo, bool exito) {
  bombaApagar();
  estado = 0;
  tocar(1);
  float umb = isnan(sesUmbral) ? umbral : sesUmbral;
  sesPhFin = ph;
  sesError = isnan(ph) ? NAN : ph - umb;
  sesMl = bombaTotalMs * CAUDAL_ML_S / 1000.0f;
  sesPulsos = nPulsos;
  sesDuracionS = sesInicioMs ? (millis() - sesInicioMs) / 1000 : 0;
  sesMotivo = motivo;
  sesExito = exito;
  sesValida = true;
  char buf[128];
  snprintf(buf, sizeof(buf), "%s. pH=%.2f umbral=%.2f error=%+.2f pulsos=%u dosis=%.1f mL",
           motivo.c_str(), ph, umb, sesError, nPulsos, sesMl);
  notificar(buf);
  blynkEvento(exito ? "dosificacion_fin" : "dosificacion_error", buf);
#ifdef USE_BLYNK
  Blynk.virtualWrite(V2, 0);
#endif
}

void siguientePulso() {
  float error = ph - umbral;
  if (error <= TOLERANCIA) { finalizar("Umbral alcanzado", true); return; }
  if (bombaTotalMs >= BOMBA_TOTAL_MAX_MS) { finalizar("Limite de solucion alcanzado", false); return; }

  uint32_t ms;
  if (ganancia <= 0) {
    ms = PULSO_INICIAL_MS << min<uint8_t>(pulsosSinEfecto, 2);      // 100, 200, 400 ms mientras se ceba
  } else {
    float objetivo = FRACCION * error / ganancia;
    if (objetivo < PULSO_MIN_MS) {
      // Incluso el pulso mínimo: ¿nos pasaría del umbral?
      if (ph - ganancia * PULSO_MIN_MS < umbral - TOLERANCIA) { finalizar("Lo mas cerca posible sin pasarse", true); return; }
      ms = PULSO_MIN_MS;
    } else {
      ms = (uint32_t)objetivo;
    }
  }
  ms = constrain(ms, PULSO_MIN_MS, PULSO_MAX_MS);
  ms = min(ms, BOMBA_TOTAL_MAX_MS - bombaTotalMs);
  if (ms < PULSO_MIN_MS) { finalizar("Limite de solucion alcanzado", false); return; }

  phAntesPulso = ph;
  pulsoActualMs = ms;
  nPulsos++;
  Serial.printf("# Pulso %u: %lu ms (error=%.2f, ganancia=%.5f pH/ms)\n", nPulsos, (unsigned long)ms, error, ganancia);
  bombaPulso(ms);
  estado = 1;
}

void solicitarInicio() {
  if (estado != 0) return;
  if (isnan(ph)) { tocar(2); notificar("Rechazado: lectura de pH invalida"); return; }
  if (ph <= umbral + TOLERANCIA) {
    tocar(2);
    notificar("Rechazado: el pH ya esta en o por debajo del umbral");
    blynkEvento("dosificacion_rechazada", "pH " + String(ph, 2) + " <= umbral " + String(umbral, 2));
#ifdef USE_BLYNK
    Blynk.virtualWrite(V2, 0);
#endif
    return;
  }
  bombaTotalMs = 0; nPulsos = 0; pulsosSinEfecto = 0;
  sesPhIni = ph; sesUmbral = umbral; sesInicioMs = millis();   // métricas de la sesión
  // La ganancia aprendida se conserva entre sesiones (misma agua/solución)
  tocar(0);
  notificar("Dosificacion iniciada: pH " + String(ph, 2) + " -> " + String(umbral, 2));
  blynkEvento("dosificacion_inicio", "pH " + String(ph, 2) + " -> umbral " + String(umbral, 2));
  siguientePulso();
}

void solicitarParo() { if (estado != 0) finalizar("Cancelado por el usuario", false); }

void actualizarControl() {
  uint32_t ahora = millis();
  if (estado == 1) {                                   // pulso en curso
    if ((int32_t)(ahora - bombaApagarEn) >= 0) {
      bombaApagar();
      estado = 2; inicioMezcla = ahora;
    }
  } else if (estado == 2) {                            // esperando propagación
    if (isnan(ph)) { finalizar("Sensor de pH sin lectura", false); return; }
    uint32_t t = ahora - inicioMezcla;
    float pend = pendientePh();
    bool estable = !isnan(pend) && fabsf(pend) < PENDIENTE_ESTABLE;
    if (t >= ESPERA_MAX_MS || (t >= ESPERA_MIN_MS && estable)) {
      float baja = phAntesPulso - ph;
      if (baja > CAMBIO_MINIMO) {
        float g = baja / pulsoActualMs;
        ganancia = (ganancia <= 0) ? g : 0.5f * ganancia + 0.5f * g;
        pulsosSinEfecto = 0;
      } else if (++pulsosSinEfecto >= MAX_PULSOS_SIN_EFECTO) {
        finalizar("La bomba no produce cambio (revisar manguera/solucion)", false); return;
      }
      Serial.printf("# Mezcla %lus: pH %.2f -> %.2f (baja %.3f)\n", (unsigned long)(t / 1000), phAntesPulso, ph, baja);
      siguientePulso();
    }
  }
}

// ---------------- Comandos serie ----------------
void procesarComando(String c) {
  c.trim(); c.toLowerCase();
  if (c == "start") solicitarInicio();
  else if (c == "stop") solicitarParo();
  else if (c.startsWith("umbral")) {
    umbral = constrain(c.substring(6).toFloat(), UMBRAL_MIN, UMBRAL_MAX); guardarPrefs();
    Serial.printf("# Umbral = %.2f\n", umbral);
  }
  else if (c == "cal7" && !isnan(adcFiltrado)) { calAdc7 = adcFiltrado; guardarPrefs(); Serial.printf("# cal7 = %.1f\n", calAdc7); }
  else if (c == "cal4" && !isnan(adcFiltrado)) { calAdc4 = adcFiltrado; guardarPrefs(); Serial.printf("# cal4 = %.1f\n", calAdc4); }
  else if (c == "calreset") { calAdc7 = CAL_ADC7_DEF; calAdc4 = CAL_ADC4_DEF; guardarPrefs(); Serial.println("# Calibracion por defecto (607 = pH 6.95)"); }
  else if (c.startsWith("cebar") && estado == 0) {
    uint32_t ms = constrain(c.substring(5).toInt(), 50, 1000);
    Serial.printf("# Cebando manguera %lu ms (%.1f mL)\n", (unsigned long)ms, ms * CAUDAL_ML_S / 1000);
    bombaPulso(ms); delay(ms); bombaApagar();
  }
  else if (c == "info") {
    Serial.printf("# pH=%.2f V=%.3f adc=%.1f cal7=%.1f cal4=%.1f umbral=%.2f ganancia=%.5f consumo=%.1f mL\n",
                  ph, voltaje, adcFiltrado, calAdc7, calAdc4, umbral, ganancia, bombaHistoricoMs * CAUDAL_ML_S / 1000);
  }
  else Serial.println("# Comandos: start | stop | umbral X | cal7 | cal4 | calreset | cebar MS | info");
}

// ---------------- Blynk ----------------
#ifdef USE_BLYNK
// V0 pH · V1 umbral (app) · V2 dosificar (app) · V3 estado · V4 voltaje
// V5 bomba · V6 mL dosificados · V7 color hex · V8 mensaje · V9 temp (V)
BLYNK_WRITE(V1) { umbral = constrain(param.asFloat(), UMBRAL_MIN, UMBRAL_MAX); guardarPrefs(); }
BLYNK_WRITE(V2) { if (param.asInt()) solicitarInicio(); else solicitarParo(); }
BLYNK_CONNECTED() { Blynk.virtualWrite(V1, umbral); Blynk.virtualWrite(V2, estado != 0); }

void enviarBlynk() {
  char hex[8]; snprintf(hex, sizeof(hex), "#%02X%02X%02X", colR, colG, colB);
  if (!isnan(ph)) Blynk.virtualWrite(V0, ph);
  Blynk.setProperty(V0, "color", hex);
  Blynk.virtualWrite(V3, NOMBRE_ESTADO[estado]);
  Blynk.virtualWrite(V4, voltaje);
  Blynk.virtualWrite(V5, bombaOn ? 1 : 0);
  Blynk.virtualWrite(V6, bombaTotalMs * CAUDAL_ML_S / 1000.0f);
  Blynk.virtualWrite(V7, hex);
  Blynk.virtualWrite(V8, ultimoMensaje);
  if (!isnan(voltTemp)) Blynk.virtualWrite(V9, voltTemp);
}
#endif

// ---------------- Página local + actualización por Wi-Fi ----------------
WebServer server(80);
bool otaError = false;

void paginaEstado() {
  char hex[8]; snprintf(hex, sizeof(hex), "#%02X%02X%02X", colR, colG, colB);
  bool sta = WiFi.status() == WL_CONNECTED;
#ifdef USE_BLYNK
  bool nube = Blynk.connected();
#else
  bool nube = false;
#endif
  String h = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<meta http-equiv='refresh' content='2'><title>Control pH ARCE</title><style>"
             "body{font-family:sans-serif;margin:16px;background:#f6f8fa;color:#1f2328}"
             ".c{max-width:480px}.ph{font-size:56px;font-weight:700;margin:4px 0}"
             "table{border-collapse:collapse;width:100%;background:#fff}td{border:1px solid #d0d7de;padding:8px}"
             "a,button{display:inline-block;margin:4px 4px 4px 0;padding:10px 14px;border:0;border-radius:6px;"
             "background:#0969da;color:#fff;text-decoration:none;font-size:15px}.r{background:#cf222e}"
             "</style></head><body><div class='c'><h2>Control de pH — ARCE</h2>";
  h += "<div style='height:56px;border-radius:8px;background:" + String(hex) + "'></div>";
  h += "<div class='ph'>pH " + (isnan(ph) ? String("--") : String(ph, 2)) + "</div><table>";
  h += "<tr><td>Umbral de apagado</td><td><b>" + String(umbral, 2) + "</b></td></tr>";
  h += "<tr><td>Estado</td><td><b>" + String(NOMBRE_ESTADO[estado]) + "</b></td></tr>";
  h += "<tr><td>Bomba</td><td>" + String(bombaOn ? "ENCENDIDA" : "apagada") + "</td></tr>";
  h += "<tr><td>Dosis de esta sesión</td><td>" + String(bombaTotalMs * CAUDAL_ML_S / 1000.0f, 1) + " mL · " + String(nPulsos) + " pulsos</td></tr>";
  h += "<tr><td>Último mensaje</td><td>" + ultimoMensaje + "</td></tr>";
  h += "<tr><td>Sonda</td><td>ADC " + String(adcFiltrado, 1) + " · " + String(voltaje, 3) + " V</td></tr>";
  h += "<tr><td>Ganancia aprendida</td><td>" + String(ganancia, 5) + " pH/ms</td></tr>";
  h += "<tr><td>Consumo total de solución</td><td>" + String(bombaHistoricoMs * CAUDAL_ML_S / 1000.0f, 1) + " mL</td></tr>";
  h += "<tr><td>Wi-Fi hackathon / Blynk</td><td>" + String(sta ? "conectado" : "sin conexión") + " / " + String(nube ? "conectado" : "sin conexión") + "</td></tr>";
  h += "<tr><td>Tiempo encendido</td><td>" + String(millis() / 1000) + " s</td></tr></table>";
  h += "<p><a href='/cmd?c=start'>Iniciar dosificación</a><a class='r' href='/cmd?c=stop'>Detener</a></p>";
  h += "<form action='/cmd'>Umbral (4.5–7): <input name='u' type='number' step='0.1' min='4.5' max='7' value='" +
       String(umbral, 1) + "' style='width:70px'> <button>Fijar</button></form>";
  h += "<p><small><a href='/update' style='background:#6e7781'>Actualizar firmware</a></small></p></div></body></html>";
  server.send(200, "text/html; charset=utf-8", h);
}

// ---------------- Dashboard propio (web_ui.h) + API JSON ----------------
String jsonNum(float v, int dec) { return (isnan(v) || isinf(v)) ? String("null") : String(v, dec); }
String jsonStr(const String& s) {
  String o; o.reserve(s.length() + 8); o += '"';
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", (uint8_t)c); o += b; }
    else o += c;
  }
  o += '"'; return o;
}

void paginaDashboard() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void apiEstado() {
  char hex[8]; snprintf(hex, sizeof(hex), "#%02X%02X%02X", colR, colG, colB);
  bool sta = WiFi.status() == WL_CONNECTED;
#ifdef USE_BLYNK
  bool nube = Blynk.connected();
#else
  bool nube = false;
#endif
  String j; j.reserve(900);
  j += "{\"ph\":";            j += jsonNum(ph, 2);
  j += ",\"adc\":";           j += jsonNum(adcFiltrado, 1);
  j += ",\"voltaje\":";       j += jsonNum(voltaje, 3);
  j += ",\"umbral\":";        j += jsonNum(umbral, 2);
  j += ",\"estado\":";        j += jsonStr(NOMBRE_ESTADO[estado]);
  j += ",\"bomba\":";         j += bombaOn ? "true" : "false";
  j += ",\"dosis_ml\":";      j += jsonNum(bombaTotalMs * CAUDAL_ML_S / 1000.0f, 1);
  j += ",\"pulsos\":";        j += String(nPulsos);
  j += ",\"ganancia\":";      j += jsonNum(ganancia, 6);
  j += ",\"consumo_total_ml\":"; j += jsonNum(bombaHistoricoMs * CAUDAL_ML_S / 1000.0f, 1);
  j += ",\"mensaje\":";       j += jsonStr(ultimoMensaje);
  j += ",\"color\":";         j += jsonStr(hex);
  j += ",\"wifi_sta\":";      j += sta ? "true" : "false";
  j += ",\"blynk\":";         j += nube ? "true" : "false";
  j += ",\"uptime_s\":";      j += String(millis() / 1000);
  j += ",\"ultima_sesion\":";
  if (!sesValida) j += "null";
  else {
    j += "{\"ph_inicial\":";  j += jsonNum(sesPhIni, 2);
    j += ",\"ph_final\":";    j += jsonNum(sesPhFin, 2);
    j += ",\"umbral\":";      j += jsonNum(sesUmbral, 2);
    j += ",\"error\":";       j += jsonNum(sesError, 2);
    j += ",\"ml\":";          j += jsonNum(sesMl, 1);
    j += ",\"pulsos\":";      j += String(sesPulsos);
    j += ",\"duracion_s\":";  j += String(sesDuracionS);
    j += ",\"motivo\":";      j += jsonStr(sesMotivo);
    j += ",\"exito\":";       j += sesExito ? "true" : "false";
    j += "}";
  }
  j += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// ---------------- Pruebas de funciones (solo en REPOSO) ----------------
//   /test?sonido=inicio|fin|rechazo     melodías del XN44
//   /test?colores=1                     recorre el indicador universal pH 0→14 en ~15 s
//   /test?rele=MS                       un pulso del relevador (50–200 ms)
//   /test?cebar=MS                      llena la manguera (50–1000 ms)
uint32_t inicioTestColores = 0;            // 0 = sin prueba de colores
String ultimoTest = "";

void paginaTest() {
  String r;
  if (estado != 0) { r = "RECHAZADO: hay una dosificacion en curso"; }
  else if (server.hasArg("sonido")) {
    String s = server.arg("sonido");
    if (s == "inicio") tocar(0); else if (s == "fin") tocar(1); else tocar(2);
    r = "Sonido: " + s;
  } else if (server.hasArg("colores")) {
    inicioTestColores = millis(); if (!inicioTestColores) inicioTestColores = 1;
    r = "Recorriendo colores pH 0 a 14 (15 s)";
  } else if (server.hasArg("rele") || server.hasArg("cebar")) {
    bool cebar = server.hasArg("cebar");
    uint32_t ms = constrain(server.arg(cebar ? "cebar" : "rele").toInt(), 50, cebar ? 1000 : 200);
    bombaPulso(ms); delay(ms); bombaApagar();
    r = String(cebar ? "Cebado " : "Pulso de relevador ") + ms + " ms (" + String(ms * CAUDAL_ML_S / 1000.0f, 1) + " mL)";
  } else {
    r = "Uso: /test?sonido=inicio|fin|rechazo · /test?colores=1 · /test?rele=100 · /test?cebar=300";
  }
  ultimoTest = r;
  Serial.println("# TEST: " + r);
  server.send(200, "text/plain; charset=utf-8", r + "\n");
}

void paginaCmd() {
  if (server.hasArg("c")) {
    if (server.arg("c") == "start") solicitarInicio();
    else if (server.arg("c") == "stop") solicitarParo();
  }
  if (server.hasArg("u")) {
    umbral = constrain(server.arg("u").toFloat(), UMBRAL_MIN, UMBRAL_MAX); guardarPrefs();
#ifdef USE_BLYNK
    if (Blynk.connected()) Blynk.virtualWrite(V1, umbral);
#endif
  }
  server.sendHeader("Location", "/"); server.send(303);
}

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
    if (estado != 0) finalizar("Actualizacion de firmware", false);   // detiene la dosificación
    bombaApagar();                                                     // relevador apagado
    uint8_t azul[3] = { 0, 0, LED_MAX }; xnWrite(ADDR_XN03, REG_RGB, azul, 3);
    Serial.printf("# OTA: recibiendo %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) otaError = true;
  } else if (up.status == UPLOAD_FILE_WRITE && !otaError) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) otaError = true;
  } else if (up.status == UPLOAD_FILE_END && !otaError) {
    if (!Update.end(true)) otaError = true;
    Serial.printf("# OTA: %u bytes, %s\n", up.totalSize, otaError ? "ERROR" : "OK");
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort(); otaError = true;
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  pinMode(BOARD_LED, OUTPUT);
  pinMode(BOARD_BUTTON, INPUT_PULLUP);
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin();
  delay(100);
  setRele(false);                                  // estado seguro
  prefs.begin("ph", false);
  calAdc7 = prefs.getFloat("cal7", calAdc7);
  calAdc4 = prefs.getFloat("cal4", calAdc4);
  umbral  = constrain(prefs.getFloat("umbral", umbral), UMBRAL_MIN, UMBRAL_MAX);
  if (fabsf(calAdc4 - calAdc7) < 20) { calAdc7 = CAL_ADC7_DEF; calAdc4 = CAL_ADC4_DEF; }   // calibración absurda
  // Red propia (página local + /update) y, con Blynk, también la red con Internet
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
#ifdef USE_BLYNK
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Blynk.config(BLYNK_AUTH_TOKEN);
#endif
  server.on("/", paginaDashboard);          // dashboard propio (web_ui.h)
  server.on("/api/estado", apiEstado);
  server.on("/simple", paginaEstado);       // página sencilla anterior (respaldo)
  server.on("/cmd", paginaCmd);
  server.on("/test", paginaTest);
  server.on("/update", HTTP_GET, paginaUpdateForm);
  server.on("/update", HTTP_POST, paginaUpdateFin, paginaUpdateDatos);
  server.begin();
  tono(1000, 80);
  Serial.println("ms,adc,voltaje,pH,umbral,estado,bomba,dosis_mL,ganancia");
}

void loop() {
  uint32_t ahora = millis();
  static uint32_t tMuestra = 0, tPh = 0, tHist = 0, tLed = 0, tLog = 0, tRele = 0;

  if (ahora - tMuestra >= 20)  { tMuestra = ahora; muestrear(); }
  if (ahora - tPh >= 250) {
    tPh = ahora; calcularPh();
    if (inicioTestColores && ahora - inicioTestColores < 15000 && estado == 0)
      colorDePh((ahora - inicioTestColores) * 14.0f / 15000.0f);   // prueba: recorre pH 0→14
    else { inicioTestColores = 0; colorDePh(ph); }
  }
  if (ahora - tHist >= 1000)   { tHist = ahora; guardarHistorial(); }
  if (ahora - tLed >= 500)     { tLed = ahora; actualizarLed(); }

  actualizarControl();
  actualizarMelodia();
  server.handleClient();

  // Seguridad: si no estamos en un pulso, el relevador debe estar apagado
  if (estado != 1 && ahora - tRele >= 2000) { tRele = ahora; setRele(false); }
  if ((bombaOn && estado != 1) || releApagadoPendiente) bombaApagar();

  // Botón BOOT: inicia / detiene
  static bool btnPrev = HIGH; static uint32_t tBtn = 0;
  bool btn = digitalRead(BOARD_BUTTON);
  if (btn != btnPrev && ahora - tBtn > 50) {
    tBtn = ahora; btnPrev = btn;
    if (btn == LOW) { if (estado == 0) solicitarInicio(); else solicitarParo(); }
  }

  if (Serial.available()) procesarComando(Serial.readStringUntil('\n'));

  if (ahora - tLog >= 500) {
    tLog = ahora;
    Serial.printf("%lu,%.1f,%.3f,%.2f,%.2f,%s,%d,%.1f,%.5f\n", (unsigned long)ahora, adcFiltrado, voltaje, ph,
                  umbral, NOMBRE_ESTADO[estado], bombaOn, bombaTotalMs * CAUDAL_ML_S / 1000.0f, ganancia);
  }

#ifdef USE_BLYNK
  static uint32_t tIntento = 0, tEnvio = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (!Blynk.connected() && estado == 0 && ahora - tIntento > 10000) { tIntento = ahora; Blynk.connect(3000); }   // solo con la bomba parada
    if (Blynk.connected()) {
      Blynk.run();
      if (ahora - tEnvio >= 1000) { tEnvio = ahora; enviarBlynk(); }
    }
  }
#endif
}
