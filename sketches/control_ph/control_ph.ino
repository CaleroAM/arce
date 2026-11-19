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
//#define USE_BLYNK

#ifdef USE_BLYNK
#include "secrets.h"
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#endif
#include <Wire.h>
#include <Preferences.h>
#include <math.h>

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
// Tabla del fabricante (ADC 10 bits, 3.3 V): pH 7 = 511, pH 4 = 674.
// Recalibrar en sitio con "cal7" (agua ≈ 7) y "cal4" (solución pH 4).
float calAdc7 = 511.0f;
float calAdc4 = 674.0f;

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

// ---------------- Indicador universal ----------------
const uint8_t COLOR_PH[15][3] = {
  {255, 0, 0},   {230, 20, 20}, {255, 40, 0},  {255, 90, 0},  {255, 140, 0},
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

void finalizar(const String& motivo, bool exito) {
  bombaApagar();
  estado = 0;
  tocar(1);
  char buf[96];
  snprintf(buf, sizeof(buf), "%s. pH=%.2f umbral=%.2f pulsos=%u dosis=%.1f mL",
           motivo.c_str(), ph, umbral, nPulsos, bombaTotalMs * CAUDAL_ML_S / 1000.0f);
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
  else if (c == "calreset") { calAdc7 = 511; calAdc4 = 674; guardarPrefs(); Serial.println("# Calibracion de fabrica"); }
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
  if (fabsf(calAdc4 - calAdc7) < 20) { calAdc7 = 511; calAdc4 = 674; }   // calibración absurda
#ifdef USE_BLYNK
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Blynk.config(BLYNK_AUTH_TOKEN);
#endif
  tono(1000, 80);
  Serial.println("ms,adc,voltaje,pH,umbral,estado,bomba,dosis_mL,ganancia");
}

void loop() {
  uint32_t ahora = millis();
  static uint32_t tMuestra = 0, tPh = 0, tHist = 0, tLed = 0, tLog = 0, tRele = 0;

  if (ahora - tMuestra >= 20)  { tMuestra = ahora; muestrear(); }
  if (ahora - tPh >= 250)      { tPh = ahora; calcularPh(); colorDePh(ph); }
  if (ahora - tHist >= 1000)   { tHist = ahora; guardarHistorial(); }
  if (ahora - tLed >= 500)     { tLed = ahora; actualizarLed(); }

  actualizarControl();
  actualizarMelodia();

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
    if (!Blynk.connected() && ahora - tIntento > 10000) { tIntento = ahora; Blynk.connect(1000); }
    if (Blynk.connected()) {
      Blynk.run();
      if (ahora - tEnvio >= 1000) { tEnvio = ahora; enviarBlynk(); }
    }
  }
#endif
}
