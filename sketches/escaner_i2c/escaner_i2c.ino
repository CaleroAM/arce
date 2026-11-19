// Escáner I2C para el kit XIDE — barre desde 0x01 (los X-NODE usan 0x01..0x0D)
// y lee WHO_AM_I (0x3E) y FW (0x38) de cada módulo encontrado.
#include <Wire.h>

#define I2C_SDA 12
#define I2C_SCL 13

const char* nombreModulo(uint8_t addr) {
  switch (addr) {
    case 0x01: return "XN01 entradas digitales";
    case 0x02: return "XN02 salidas digitales";
    case 0x03: return "XN03 E/S analogicas (pot + RGB)";
    case 0x04: return "XN04 temp/hum/luz";
    case 0x0D: return "XN13 relevador";
    case 0x2C: return "XN44 buzzer";
    default:   return "desconocido";
  }
}

bool leer(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

void escanear() {
  Serial.println("\n=== Escaneando I2C 0x01-0x7E (SDA=12, SCL=13) ===");
  int encontrados = 0;
  for (uint8_t a = 1; a < 0x7F; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0) continue;
    encontrados++;
    uint8_t who[2] = {0, 0}, fw[3] = {0, 0, 0};
    bool okWho = leer(a, 0x3E, who, 2);
    bool okFw  = leer(a, 0x38, fw, 3);
    Serial.printf("0x%02X  %-32s", a, nombreModulo(a));
    if (okWho) Serial.printf("  WHO_AM_I modelo=%u rev=%u", who[0], who[1]);
    if (okFw)  Serial.printf("  FW=%u.%u.%u", fw[0], fw[1], fw[2]);
    Serial.println();
  }
  Serial.printf("=== %d dispositivo(s). Repite en 10 s ===\n", encontrados);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(16, OUTPUT);
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin();
}

void loop() {
  digitalWrite(16, !digitalRead(16));
  escanear();
  delay(10000);
}
