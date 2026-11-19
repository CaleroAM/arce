// =====================================================================
//  Clorador inteligente — Equipo ARCE — Hackatón Puebla 2026
//  Kit XIDE: XC01 (ESP32-S3) + XN03 (pot/RGB/ADC) + XN13 (relevador)
//            + XN44 (buzzer) [+ XN01 flujo opcional]
//
//  - Lazo cerrado: dosificación proporcional por pulsos con el relevador.
//  - Seguridad: corte por sobrecloración, sin flujo, falla de sensor
//    y bomba encendida demasiado tiempo (se rearma con el botón BOOT).
//  - El control es LOCAL: funciona sin Wi-Fi. Blynk es opcional.
//
//  Para activar Blynk: copia secrets.h.example a secrets.h, rellénalo
//  y descomenta la línea USE_BLYNK.
// =====================================================================
//#define USE_BLYNK

#ifdef USE_BLYNK
#include "secrets.h"            // BLYNK_TEMPLATE_ID/NAME/AUTH_TOKEN, WIFI_SSID, WIFI_PASS
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#endif
#include <Wire.h>
#include <math.h>

// ---------------- Pines y direcciones ----------------
#define I2C_SDA   12
#define I2C_SCL   13
#define LED_PLACA 16
#define BTN_BOOT  0

const uint8_t ADDR_XN01 = 0x01;
const uint8_t ADDR_XN03 = 0x03;
const uint8_t ADDR_XN13 = 0x0D;
const uint8_t ADDR_XN44 = 0x2C;

// ---------------- Opciones ----------------
#define SIMULAR_CLORO_CON_POT 1   // 1 = el potenciómetro del XN03 simula el sensor de cloro
#define USAR_SENSOR_FLUJO     0   // 1 = XN01 entrada 1 indica flujo
#define FLUJO_ACTIVO_EN_1     1   // invertir si el XN01 reporta 0 con flujo (verificar en hardware)
#define USAR_SENSOR_PH        0   // 1 = sonda pH analógica en XN03 IN1 (ADC1, reg 0x21)

// ---------------- Parámetros del proceso ----------------
const float CL_MIN_NORMA = 0.2f;      // mg/L (NOM-127-SSA1-2021)
const float CL_MAX_NORMA = 1.5f;      // mg/L
const float CL_MAX_SIM   = 2.0f;      // pot al 100 % = 2.0 mg/L
float setpointCl         = 0.8f;      // mg/L objetivo
const float KP           = 2.0f;      // duty = KP * error  (error 0.5 mg/L -> 100 %)
const float BANDA_OK     = 0.15f;     // mg/L alrededor del setpoint
const uint32_t VENTANA_MS            = 10000;  // ventana de pulsos de dosificación
const uint32_t MAX_BOMBA_CONTINUA_MS = 60000;  // bomba al 100 % sin efecto -> falla
const float PKA_HOCL     = 7.5f;
const uint8_t LED_MAX    = 100;       // subir a 255 si el RGB del XN03 usa 0–255

// Calibración de pH en 2 puntos (medir V con buffers pH 7 y pH 4)
float PH_V7 = 1.50f, PH_V4 = 2.03f;
float phManual = 7.0f;                // se usa si no hay sonda (ajustable desde Blynk V11)

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

// ---------------- Actuadores ----------------
bool releEstado = false;
bool setRele(bool on) {
  uint8_t v = on ? 0x01 : 0x00;
  bool ok = xnWrite(ADDR_XN13, 0x01, &v, 1);
  if (ok) releEstado = on;
  return ok;
}
void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t d[3] = { r, g, b };
  xnWrite(ADDR_XN03, 0x01, d, 3);
}
void beep(uint16_t hz, uint16_t ms) {
  uint8_t d[4] = { uint8_t(hz >> 8), uint8_t(hz), uint8_t(ms >> 8), uint8_t(ms) };
  xnWrite(ADDR_XN44, 0x01, d, 4);
}

// ---------------- Sensores ----------------
float leerCloro() {
#if SIMULAR_CLORO_CON_POT
  uint8_t pot;
  if (!xnRead(ADDR_XN03, 0x10, &pot, 1) || pot > 100) return NAN;
  return pot * CL_MAX_SIM / 100.0f;
#else
  // TODO: sensor real (ORP/amperométrico) -> convertir a mg/L
  return NAN;
#endif
}

float leerPH() {
#if USAR_SENSOR_PH
  uint16_t raw;
  if (!xnRead16(ADDR_XN03, 0x21, raw) || raw > 1023) return NAN;
  float v = raw * 3.3f / 1023.0f;
  return 7.0f + (PH_V7 - v) * 3.0f / (PH_V4 - PH_V7);
#else
  return phManual;
#endif
}

bool hayFlujo() {
#if USAR_SENSOR_FLUJO
  uint8_t in;
  if (!xnRead(ADDR_XN01, 0x01, &in, 1)) return false;   // ante duda: sin flujo = no dosificar
  bool bit = in & 0x01;
  return FLUJO_ACTIVO_EN_1 ? bit : !bit;
#else
  return true;
#endif
}

float fraccionHOCl(float ph) { return 1.0f / (1.0f + powf(10.0f, ph - PKA_HOCL)); }

// ---------------- Estado ----------------
enum Estado { OK, CORRIGIENDO, CLORO_BAJO, SOBRECLORACION, SIN_FLUJO, FALLA };
const char* NOMBRE_ESTADO[] = { "OK", "CORRIGIENDO", "CLORO_BAJO", "SOBRECLORACION", "SIN_FLUJO", "FALLA" };

Estado estado = OK, estadoPrevio = OK;
bool fallaEnclavada = false;
const char* motivoFalla = "";
float cloro = NAN, ph = NAN, hocl = NAN, duty = 0;
uint32_t inicioVentana = 0, bombaOnDesde = 0;

void enclavarFalla(const char* motivo) { fallaEnclavada = true; motivoFalla = motivo; }

#ifdef USE_BLYNK
BLYNK_WRITE(V10) { setpointCl = constrain(param.asFloat(), CL_MIN_NORMA, CL_MAX_NORMA); }
BLYNK_WRITE(V11) { phManual = constrain(param.asFloat(), 0.0f, 14.0f); }
BLYNK_CONNECTED() { Blynk.syncVirtual(V10, V11); }

void enviarBlynk() {
  Blynk.virtualWrite(V0, cloro);
  Blynk.virtualWrite(V1, ph);
  Blynk.virtualWrite(V2, hocl);
  Blynk.virtualWrite(V3, releEstado ? 1 : 0);
  Blynk.virtualWrite(V4, NOMBRE_ESTADO[estado]);
  Blynk.virtualWrite(V5, duty * 100.0f);
}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(LED_PLACA, OUTPUT);
  pinMode(BTN_BOOT, INPUT_PULLUP);
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin();
  delay(200);
  setRele(false);                       // estado seguro al arrancar
  setRGB(0, 0, LED_MAX);
  beep(1000, 100);
#ifdef USE_BLYNK
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Blynk.config(BLYNK_AUTH_TOKEN);
#endif
  inicioVentana = millis();
  Serial.println("ms,cloro_mgL,pH,HOCl_mgL,setpoint,duty_pct,bomba,estado");
}

void loop() {
  uint32_t ahora = millis();

  // --- Rearme de falla con BOOT ---
  if (fallaEnclavada && digitalRead(BTN_BOOT) == LOW) {
    fallaEnclavada = false; motivoFalla = "";
    Serial.println("# Falla rearmada con BOOT");
  }

  // --- Medición ---
  cloro = leerCloro();
  ph    = leerPH();
  hocl  = (isnan(cloro) || isnan(ph)) ? NAN : cloro * fraccionHOCl(ph);
  bool flujo = hayFlujo();
  if (isnan(cloro)) enclavarFalla("lectura de cloro invalida");

  // --- Decisión ---
  duty = 0;
  if (fallaEnclavada)              estado = FALLA;
  else if (!flujo)                 estado = SIN_FLUJO;
  else if (cloro > CL_MAX_NORMA)   estado = SOBRECLORACION;
  else {
    duty = constrain(KP * (setpointCl - cloro), 0.0f, 1.0f);
    if (duty < 0.05f) duty = 0;        // evita pulsos < 0.5 s que desgastan el relevador
    if (cloro < CL_MIN_NORMA)                     estado = CLORO_BAJO;
    else if (fabsf(cloro - setpointCl) > BANDA_OK) estado = CORRIGIENDO;
    else                                           estado = OK;
  }

  // --- Actuación por pulsos ---
  if (ahora - inicioVentana >= VENTANA_MS) inicioVentana = ahora;
  bool quiereBomba = (ahora - inicioVentana) < (uint32_t)(duty * VENTANA_MS);
  if (quiereBomba && !releEstado) bombaOnDesde = ahora;
  if (quiereBomba && ahora - bombaOnDesde > MAX_BOMBA_CONTINUA_MS) {
    enclavarFalla("bomba encendida demasiado tiempo sin efecto");
    quiereBomba = false;
  }
  static uint32_t ultimoRefresco = 0;
  if (quiereBomba != releEstado || ahora - ultimoRefresco > 5000) {
    if (!setRele(quiereBomba) && quiereBomba) enclavarFalla("sin comunicacion con relevador");
    ultimoRefresco = ahora;
  }

  // --- Señalización local ---
  static uint32_t ultimaSenal = 0;
  if (ahora - ultimaSenal > 500) {
    ultimaSenal = ahora;
    switch (estado) {
      case OK:             setRGB(0, LED_MAX, 0); break;
      case CORRIGIENDO:    setRGB(LED_MAX, LED_MAX / 2, 0); break;
      case SIN_FLUJO:      setRGB(0, 0, LED_MAX); break;
      default:             setRGB(LED_MAX, 0, 0); break;   // bajo, sobre, falla
    }
    digitalWrite(LED_PLACA, releEstado);
  }
  static uint32_t ultimoBeep = 0;
  bool alarma = (estado == FALLA || estado == SOBRECLORACION || estado == CLORO_BAJO);
  if (alarma && ahora - ultimoBeep > 3000) { ultimoBeep = ahora; beep(2000, 200); }

  // --- Registro serie ---
  static uint32_t ultimoLog = 0;
  if (ahora - ultimoLog > 1000) {
    ultimoLog = ahora;
    Serial.printf("%lu,%.2f,%.2f,%.2f,%.2f,%.0f,%d,%s%s%s\n", (unsigned long)ahora,
                  cloro, ph, hocl, setpointCl, duty * 100, releEstado, NOMBRE_ESTADO[estado],
                  fallaEnclavada ? " - " : "", motivoFalla);
  }

#ifdef USE_BLYNK
  static uint32_t ultimoIntento = 0, ultimoEnvio = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (!Blynk.connected() && ahora - ultimoIntento > 10000) { ultimoIntento = ahora; Blynk.connect(1000); }
    if (Blynk.connected()) {
      Blynk.run();
      if (ahora - ultimoEnvio > 2000) { ultimoEnvio = ahora; enviarBlynk(); }
      if (estado != estadoPrevio && alarma)
        Blynk.logEvent("alarma", String(NOMBRE_ESTADO[estado]) + " cloro=" + String(cloro, 2));
    }
  }
#endif
  estadoPrevio = estado;
  delay(50);
}
