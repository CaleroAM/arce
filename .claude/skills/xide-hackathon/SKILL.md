---
name: xide-hackathon
description: Programar, depurar y cargar firmware al kit XIDE de MICROSIDE (controlador XC01 ESP32-S3 sobre la placa base XBI01 con módulos X-NODE XN01/XN02/XN03/XN04/XN13/XN14/XN44 por I2C) para el Hackatón Puebla 2026, cuyo reto es la cloración y el monitoreo de la calidad del agua. Úsala siempre que el usuario pida código Arduino/ESP32, un sketch, Blynk, sensores, relevador, buzzer, escaneo I2C, cargar o compilar firmware, o lógica de dosificación de cloro, pH, ORP o turbidez en este proyecto.
---

# Kit XIDE — skill del equipo ARCE

La referencia completa (registros, erratas, propuestas y plan de las 6 h) está en `XIDE_DISPOSITIVO.md`, en la raíz del proyecto. Léela cuando necesites un registro o un detalle que no esté aquí.

## RETO OFICIAL (prioridad)
El reto real es **control de pH**, no de cloro (sección "⭐ RETO OFICIAL" de `XIDE_DISPOSITIVO.md`).

- **Objetivo:** bajar el agua (pH 7–7.5) hasta un umbral de 4.5–7 elegido por el usuario, dosificando una solución de pH 4.
- **Requisitos:**
  - la bomba se activa en remoto desde Blynk;
  - se rechaza la activación si el pH ya es menor o igual que el umbral;
  - la bomba se apaga sola al llegar al umbral;
  - dos sonidos distintos, uno al iniciar y otro al terminar;
  - indicador de color universal;
  - filtro contra el ruido de la lectura.
- **Hardware:**
  - sonda Po → XN03 IN1 (registro `0x21`, ADC de 10 bits). Con la tabla del fabricante, un ADC de 511 es pH 7 y uno de 674 es pH 4;
  - bomba de diafragma de 12 V a 33 mL/s, conmutada por el XN13. El pulso mínimo es de 50 ms (unos 1.65 mL);
  - solo hay 100 mL de solución, unos 3 s de bomba en total.
- **Gana quien se acerque más al umbral sin pasarse.** Hay retardo de propagación, así que se usa el control "pulsa y espera".
- **El sketch base es `sketches/control_ph/control_ph.ino`.** Compila con y sin `USE_BLYNK`. Modifica ese archivo en lugar de empezar de cero.
- `sketches/clorador_arce/` es la versión anterior, pensada para cloro. Úsala solo como referencia.

## Hechos que no debes olvidar

- **Placa:** ESP32-S3 (QFN56, rev v0.2) con **16 MB de flash y sin PSRAM**, BLE 5 y USB nativo `303a:1001` (`/dev/ttyACM0`).
- **I2C:** `Wire.setPins(12, 13); Wire.begin();` (SDA = 12, SCL = 13). Es el mismo bus para todos los módulos, a 100 kHz.
- **UART del mikroBUS:** `Serial2.begin(115200, SERIAL_8N1, 9, 10)`. Todos los comandos terminan en `"\r\n"`.
- **Otros pines:** LED de placa en GPIO16, AN en 4 (ADC1, compatible con Wi-Fi), PWM en 5, INT en 7, CS en 6, RST en 15, SPI en 8/18/17 y BOOT en 0.
- **Señales que no vienen conectadas:** en la XBI01, AN, RST, CS, PWM e INT solo funcionan si se unen con jumpers en el header de 40 pines.
- **Direcciones I2C:** coinciden con el número del modelo. XN01 `0x01`, XN02 `0x02`, XN03 `0x03`, XN04 `0x04`, XN13 `0x0D` y XN44 `0x2C`. El XN14 es una fuente de alimentación y no tiene I2C.
- **Registros comunes:** STAT `0x37`, FW `0x38` (3 bytes), WHO_AM_I `0x3E` (2 bytes: modelo y revisión). **No escribas nunca** TW_ADD `0x3A` ni UART_ID `0x39`: se guardan en la memoria no volátil y cambian la dirección del módulo.
- **XN03:** pot `0x10` (0–100), ADC_POT `0x20` y ADC1–3 en `0x21`–`0x23` (2 bytes MSB, 0–1023, para un máximo de 3.3 V). El RGB se escribe a partir de `0x01` (bytes R, G, B). El rango de esos bytes no está documentado: pruébalo.
- **XN04:** TEMP `0x01` y HUM `0x02` se dividen entre 100; LUM `0x03` va en lux. Los valores 0xFFFC o mayores son centinelas de error y deben dar `NAN`.
- **XN13:** el registro `0x01` acepta `0x01` (encendido) y `0x00` (apagado).
- **XN44:** el registro `0x01` recibe 4 bytes `[freqH, freqL, durH, durL]` (31 a 20000 Hz y 1 a 60000 ms).
- **XN01 y XN02:** el registro `0x01` es 1 byte; el bit 0 es el canal 1.

## Reglas al escribir código

1. Usa estos helpers en lugar de `Wire` suelto:
   ```cpp
   bool xnWrite(uint8_t a, uint8_t r, const uint8_t* d, size_t n){Wire.beginTransmission(a);Wire.write(r);Wire.write(d,n);return Wire.endTransmission()==0;}
   bool xnRead(uint8_t a, uint8_t r, uint8_t* b, size_t n){Wire.beginTransmission(a);Wire.write(r);if(Wire.endTransmission()!=0)return false;if(Wire.requestFrom(a,(uint8_t)n)!=n)return false;for(size_t i=0;i<n;i++)b[i]=Wire.read();return true;}
   bool xnRead16(uint8_t a, uint8_t r, uint16_t& o){uint8_t b[2];if(!xnRead(a,r,b,2))return false;o=(uint16_t(b[0])<<8)|b[1];return true;}
   ```
   No uses `(Wire.read()<<8)|Wire.read()`: en C++ el orden de evaluación de las dos lecturas no está garantizado.
2. Revisa el valor de retorno de cada operación I2C. Si un sensor falla, devuelve `NAN` y no 0.
3. **El control debe ser local.** Nunca hagas que la dosificación dependa de Blynk o de la Wi-Fi: usa `Blynk.config()` y `Blynk.connect(timeout)`, no `Blynk.begin()`, que se bloquea. El `loop()` no puede tener `delay()` largos; usa `millis()` o `BlynkTimer`.
4. Los `#define BLYNK_TEMPLATE_ID`, `BLYNK_TEMPLATE_NAME` y el token van **antes** de `#include <BlynkSimpleEsp32.h>`. Pon los secretos en `secrets.h`, añádelo a `.gitignore` y nunca escribas tokens ni contraseñas reales en archivos versionados.
5. **Seguridad de la dosificación (obligatoria en cualquier lógica de cloro):**
   - un tiempo máximo de bomba encendida;
   - corte si el cloro supera 1.5 mg/L;
   - no dosificar si no hay flujo, cuando haya sensor de flujo;
   - un estado seguro con el relevador apagado al arrancar y ante cualquier error de lectura.
6. Las entradas analógicas aceptan como máximo 3.3 V. Si el sensor entrega 5 V (turbidez, caudalímetro), indica en el código que hace falta un divisor resistivo.
7. Rangos de referencia (NOM-127-SSA1-2021): cloro libre 0.2–1.5 mg/L, pH 6.5–8.5, turbiedad ≤ 4 UTN. Para el cloro efectivo, fracción de HOCl = 1/(1+10^(pH−7.5)).
8. Imprime por `Serial` a 115200 con etiquetas claras para que se pueda depurar en vivo.

## Compilar y cargar desde la terminal (NixOS)

```bash
nix-shell -p arduino-cli esptool --run '
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=disabled"
arduino-cli compile -b "$FQBN" <sketch_dir> && arduino-cli upload -b "$FQBN" -p /dev/ttyACM0 <sketch_dir>'
```

- **Entorno ya verificado** (core esp32 4.0.0 + Blynk 1.4.0 en `~/.arduino15`): el FQBN de arriba compila `sketches/escaner_i2c` y `sketches/clorador_arce`, con y sin `USE_BLYNK`. Sketches base del proyecto: `sketches/escaner_i2c/` (detectar módulos) y `sketches/clorador_arce/` (control de cloración + seguridad + Blynk opcional; secretos en `secrets.h`, a partir de `secrets.h.example`).
- **Si `/dev/ttyACM0` no aparece:** comprueba que el cable sea de datos y que no esté conectado a un hub, o pon la placa en modo de descarga manual (mantener BOOT, pulsar RST, soltar BOOT).
- **Para ver la salida:** `arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200`. Si no sale nada, falta `CDCOnBoot=cdc`.
- **Para identificar el chip sin borrar nada:** `esptool --port /dev/ttyACM0 flash-id`.
- **Antes de sobrescribir un firmware que no sea nuestro:** respáldalo con `esptool --port /dev/ttyACM0 read-flash 0 ALL backup/flash_<fecha>.bin`. Para restaurarlo, usa `write-flash 0 <archivo>`.

## Flujo cuando llegue el kit

1. Cargar el escáner I2C de `XIDE_DISPOSITIVO.md` §4.3. Barre desde `0x01`, porque los escáneres típicos empiezan en `0x08` y no verían estos módulos. El escáner lee WHO_AM_I y FW de cada dirección.
2. Comparar el resultado con la tabla de módulos y anotar en `XIDE_DISPOSITIVO.md` el contenido real del kit.
3. Probar cada módulo por separado antes de integrarlos.
4. Integrar en este orden: control local (propuesta A), luego Blynk (B) y por último lo innovador (C y D), haciendo un commit cada vez que algo funcione.
