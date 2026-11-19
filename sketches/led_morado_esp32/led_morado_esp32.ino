// Prueba: ¿el LED de la XC01 (GPIO16) es RGB direccionable (WS2812)?
// Si lo es, se verá MORADO fijo. Si es un LED de un solo color, se verá apagado o muy tenue.
#define BOARD_LED 16

void setup() {
  Serial.begin(115200);
  delay(1500);
}

void loop() {
  rgbLedWrite(BOARD_LED, 128, 0, 128);   // morado (R, G, B) en protocolo WS2812
  Serial.println("Enviando morado (128,0,128) al GPIO16 como LED RGB WS2812");
  delay(1000);
}
