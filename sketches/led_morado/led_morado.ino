// Prueba: LED RGB del XN03 en morado + parpadeo del LED de placa (GPIO16)
#include <Wire.h>

#define I2C_SDA   12
#define I2C_SCL   13
#define BOARD_LED 16
const uint8_t ADDR_XN03 = 0x03;
const uint8_t REG_RGB   = 0x01;   // bytes: R, G, B, (escala opcional)

// Morado: rojo medio + azul alto. Si se ve tenue, prueba con valores 0–255 (p. ej. 128, 0, 255).
uint8_t R = 50, G = 0, B = 100;

bool ponerColor(uint8_t r, uint8_t g, uint8_t b) {
  Wire.beginTransmission(ADDR_XN03);
  Wire.write(REG_RGB);
  Wire.write(r); Wire.write(g); Wire.write(b);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(BOARD_LED, OUTPUT);
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin();
}

void loop() {
  bool ok = ponerColor(R, G, B);
  Serial.println(ok ? "XN03 OK: LED RGB en morado"
                    : "XN03 NO responde en 0x03 (¿montado en la XBI01 y alimentado?)");
  digitalWrite(BOARD_LED, !digitalRead(BOARD_LED));   // el LED de placa parpadea = el programa corre
  delay(1000);
}
