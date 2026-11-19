# Kit XIDE (MICROSIDE) — Guía de referencia para el Hackatón Puebla 2026

> Equipo **ARCE** (Instituto Tecnológico de Puebla) · Hackatón: **jueves 19 nov 2026**
> Fuentes: escaneo directo de nuestra placa (14 nov), [curso introductorio MICROSIDE](https://docs.microside.com/workshops/hackathon-curso-introductorio),
> fichas de producto en docs.microside.com y la [convocatoria oficial](https://hackatonpuebla.murointeractivo.com/assets/convocatoria.docx).
> ⚠️ = dato que la documentación oficial tiene inconsistente; verificarlo en hardware.

---

## 1. El evento en 30 segundos

| | |
|---|---|
| **Reto** | *Sistema inteligente para la cloración y monitoreo de la calidad del agua* |
| **Ejes** | **Instrumentación** (sensores y mediciones) · **Automatización** (supervisión y control inteligente de la cloración) · **Monitoreo** (convertir datos en decisiones) |
| **Horario** | 8:00 registro · 8:30 inauguración · **9:00–17:00 desarrollo (6 h)** · luego presentación, evaluación y premiación |
| **Sede** | Gimnasio del ITP, Av. Tecnológico 420, Col. Maravillas |
| **Evaluación** | Comité de expertos con rúbrica **técnica, funcional y de innovación** (sin ponderaciones públicas; se dan a conocer ese día) |
| **Premio** | Solo 1.er lugar (se anuncia el día del evento) |
| **Kit** | Se entrega *durante* el evento: “kit tecnológico completo” XIDE (contenido no publicado) |
| **Organizan** | Sec. de Economía y Turismo (Ciudad de Puebla), Agua de Puebla, ITP |

**Lo que valora el reto:** que el sistema *mida*, *decida/actúe* (dosificación) y *muestre/alerte*. Una demo en vivo con lazo cerrado vale más que muchas pantallas.

---

## ⭐ RETO OFICIAL: control de pH (manda sobre las secciones 6 y 7)

Fuente: [Reto - Monitoreo y control de calidad de agua IoT](https://docs.microside.com/workshops/hackathon/reto-monitoreo-y-control-de-calidad-de-agua-iot). **Sketch listo:** `sketches/control_ph/control_ph.ino`.

**Requerimientos**
1. Convertir la salida analógica de la sonda a pH, **con filtro contra el ruido eléctrico**.
2. Dosificar una **solución de pH 4** con la bomba para bajar el agua (pH 7–7.5) hasta un **umbral de apagado entre 7 y 4.5**, que define el usuario.
   - La activación es **remota**. **Si el pH ya está por debajo del umbral, se impide activar la bomba.**
   - El sistema regula el tiempo de encendido y **apaga automáticamente al llegar al umbral**.
   - **Gana quien se acerque más al umbral sin pasarse.** La solución tarda en propagarse, así que el algoritmo debe tenerlo en cuenta.
3. Dos **alertas sonoras distintas**: una al empezar la dosificación y otra al terminarla.
4. **Indicador universal de colores** para el pH (LED RGB del XN03).
5. **Dashboard en Blynk.Cloud** para ver el pH, fijar el umbral y activar la bomba. El *widget HTML* da un aspecto más profesional.

**Kit (maqueta "Control de Calidad de Agua IoT")**: XC01 + XN03 + XN13 + XN44 + XN14 sobre la XBI01, más:
- fuente de **12 V a 12 W**;
- **bomba de diafragma de 12 V y 3 W, con un caudal de 33 mL/s**. El relevador conmuta en un mínimo de **50 ms**, lo que equivale a unos **1.65 mL por pulso**;
- **100 mL de solución de pH 4**, que son unos **3 s de bomba en total**;
- **200 mL de agua** con pH de 7 a 7.5, que se puede rellenar;
- **sonda de pH con tarjeta analógica precalibrada** (PH-4502C).

**Conexiones (diagramas oficiales)**

| Desde | Hacia |
|---|---|
| Sonda **Po** (salida de pH) | **XN03 IN1** → registro `0x21` (ADC de 10 bits) |
| Sonda **To** (salida de temperatura) | XN03 IN2 → registro `0x22` |
| Sonda V+ / G | XN03 3V3 / GND |
| Fuente 12 V (+) | **XN13 COM** |
| **XN13 NA** | Bomba (+) |
| Bomba (−) | Fuente 12 V (−) |
| Fuente 12 V (−) / (+) | XN14 GND(−) / 12V(+) |

**Tabla del fabricante (ADC de 10 bits, 3.3 V):** la relación es lineal. Con un ADC de 511 el pH es 7 y con 674 es 4, de modo que `pH = 7 + (ADC − 511) × (−3) / (674 − 511)`. Es lo mismo que `pH ≈ (892 − ADC) / 54.43`.

| pH | 0 | 2 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 12 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ADC | 892 | 783 | 674 | 620 | 565 | 511 | 456 | 402 | 347 | 238 | 130 |
| Color | rojo | rojo | naranja | amar.-naranja | amarillo | verde | verde-azul | azul | azul osc. | violeta | violeta osc. |

**Cómo resuelve el reto `control_ph.ino`**
- **Filtro:** toma muestras cada 20 ms y calcula la **media recortada** de 25 muestras (descarta el 25 % más alto y el 25 % más bajo). Encima aplica un suavizado EMA.
- **Control "pulsa y espera" adaptativo:**
  1. Da un pulso y espera la propagación: al menos 6 s, hasta que la lectura se estabilice por debajo de 0.01 pH/s, con un máximo de 30 s.
  2. Mide cuánto bajó el pH y **aprende la ganancia** (pH por ms de bomba).
  3. El siguiente pulso es `0.6 × error / ganancia`.
  4. Si incluso el pulso mínimo de 50 ms lo pasaría del umbral, se detiene ("lo más cerca posible sin pasarse").
- **Por qué es seguro:** a medida que el pH se acerca a 4 (el valor de la solución), cada mL baja el pH menos. La ganancia aprendida en el pulso anterior es entonces mayor que la real, así que los pulsos salen **más cortos, no más largos**, y el error va hacia el lado seguro.
- **Protecciones:**
  - rechaza la activación si el pH ya es menor o igual que el umbral;
  - limita el consumo a 2.5 s de bomba (unos 82 mL);
  - si 5 pulsos seguidos no producen cambio, se detiene (manguera sin cebar o sin solución);
  - el relevador se fuerza a apagado cada 2 s fuera de los pulsos.
- **Sonidos:** melodía ascendente al empezar, descendente al terminar y dos tonos graves al rechazar.
- **Calibración en sitio** (por el monitor serie, se guarda en NVS): `cal7` con la sonda en el agua y `cal4` con la sonda en la solución.
- **Otros comandos serie:** `start`, `stop`, `umbral 5.5`, `cebar 300` (llena la manguera antes de la primera dosificación) e `info`.
- **Control local:** el botón BOOT inicia y detiene la dosificación sin necesidad de la nube.
- **Blynk:** los datastreams V0 a V9 y los eventos están en `sketches/control_ph/secrets.h.example`. El firmware cambia el color del indicador de pH (V0) con `setProperty` y envía el código hexadecimal del color en V7 para el widget HTML.

**Puntos a ajustar con el kit en la mano**
1. Comprobar que el XN03 entrega el ADC en 0–1023. Hay que ver el valor de `adc` en el CSV con la sonda en el agua: debería rondar 511.
2. **Cebar la manguera** antes de la demo con `cebar 200`. Si no, los primeros pulsos solo llenan el tubo.
3. Si la mezcla es muy lenta (no hay agitador), subir `ESPERA_MIN_MS`. Agitar con suavidad el recipiente ayuda.
4. Si cada pulso baja el pH demasiado, bajar `PULSO_INICIAL_MS` a 50 o `FRACCION` a 0.4.

**Reparto de trabajo que sugiere el reto:** una persona para la maqueta (cableado), otra para el firmware y otra para el dashboard web y móvil, que es "lo que más impacto causa". Se permite usar IA: la documentación tiene un botón *Copiar* para LLMs. **Los evaluadores harán preguntas sobre el proyecto, así que hay que entender el algoritmo.**

---

## 2. Hardware: qué es cada cosa

### 2.1 Arquitectura

```
                 XBI01 "IIoTrainer" (placa base, 5 zócalos mikroBUS)
   ┌───────────────────────────────────────────────────────────────────┐
   │  Zócalo 1    Zócalo 2    Zócalo 3    Zócalo 4    Zócalo 5         │
   │  [X-NODE]    [X-NODE]    [X-NODE]    [X-NODE]    [XC01 ESP32-S3]  │
   │      │           │           │           │           │            │
   │      └───────────┴─── I2C (SDA 12 / SCL 13) + UART + SPI ┘        │
   │                    compartidos en TODOS los zócalos               │
   │  2× Qwiic (JST)   5 bloques de clemas (nodos A–E aislados)        │
   └───────────────────────────────────────────────────────────────────┘
```

- **Cada X-NODE es un módulo “inteligente”** con su propio microcontrolador: el ESP32 no lee el sensor directamente, sino que **le pregunta por I2C** (o UART) a una dirección fija y lee *registros*.
- **SCK, MISO, MOSI, TX, RX, SDA, SCL** están conectados en todos los zócalos.
- **AN, RST, CS, PWM, INT NO están conectados** salvo que se unan con *jumpers* en el doble header de 40 pines de la XBI01.
- Montar el **XC01 en el zócalo 5** (es el que tiene más señales disponibles).
- Resistencias PU/PD de 10 kΩ disponibles por señal en la XBI01.

### 2.2 Controlador XC01 R5 — lo que verificamos en NUESTRA placa (esptool, 14-nov)

| Parámetro | Valor real medido |
|---|---|
| Chip | **ESP32-S3 (QFN56) rev v0.2**, doble núcleo 240 MHz, Wi-Fi 2.4 GHz |
| Bluetooth | **BLE 5** (la ficha dice “BT 4.2 BR/EDR” ⚠️ — el S3 **no** tiene Bluetooth clásico) |
| Flash | **16 MB** (quad, 3.3 V) |
| PSRAM | **No tiene** → en el IDE dejar **PSRAM: Disabled** |
| USB | USB nativo Serial/JTAG (VID:PID `303a:1001`), aparece como `/dev/ttyACM0` (Linux) o `COMx` (Windows) |
| Firmware de fábrica/curso | Sketch Arduino (core esp32 **3.3.x**, IDF 5.5) con **Blynk 1.4.0**, conecta a la Wi-Fi “Alumnos” |
| Particiones de ese firmware | Esquema *Default 4MB* (desperdicia 12 MB) |
| Detalle | Al arrancar **no imprimía nada por USB** → se compiló con *USB CDC On Boot: Disabled* |

> **Seguridad:** ese firmware lleva el *auth token* de Blynk y la contraseña Wi-Fi en texto plano dentro del binario. No subas tokens a GitHub: usa un `secrets.h` en `.gitignore`.

### 2.3 Mapa de pines (plantilla oficial XC01 R5)

| mikroBUS | GPIO | Uso |
|---|---|---|
| **SDA** | **12** | I2C — bus de todos los X-NODE |
| **SCL** | **13** | I2C |
| RX | 9 | `Serial2` (UART mikroBUS) |
| TX | 10 | `Serial2` |
| SCK | 8 | SPI |
| MISO | 18 | SPI |
| MOSI | 17 | SPI |
| AN | 4 | Analógico (ADC1 → **sí funciona con Wi-Fi activo**) |
| PWM | 5 | PWM |
| INT | 7 | Interrupción |
| CS | 6 | SPI CS |
| RST | 15 | Reset mikroBUS |
| **LED de placa** | **16** | LED integrado |
| BOOT | 0 | Botón BOOT (pull-up) |

> En el ESP32-S3, **ADC2 (GPIO 11–20) no se puede usar con Wi-Fi encendido**. Para sensores analógicos directos usa GPIO 1–10 (ADC1), p. ej. AN = GPIO4.

### 2.4 Módulos X-NODE (direcciones I2C = número del modelo en decimal)

| Módulo | Qué es | I2C | ¿Útil para el reto? |
|---|---|---|---|
| **XN01** | 8 entradas digitales (switch + clema, pull-up 10 kΩ, contacto seco a GND) | `0x01` | Flotadores de nivel, interruptor de flujo, botón de paro |
| **XN02** | 8 salidas digitales 3.3 V TTL (máx **20 mA** c/u ⚠️) | `0x02` | Indicadores, señales a drivers |
| **XN03** | Pot 5 kΩ + LED RGB + **3 entradas analógicas** (10 bit, **máx 3.3 V**) + 3 salidas PWM/analógicas | `0x03` | **Leer sensores analógicos** (pH, TDS, turbidez) · **pot = simulador de sensor** · semáforo RGB |
| **XN04** | SHT40 (T/HR, ±0.2 °C) + LTR-329 (luz 0.01–64 klx) | `0x04` | Temperatura ambiente/caseta (¡no del agua!) |
| **XN13** | Relevador Panasonic DK1A1B-5V, 8 A, 250 VAC / 30 VDC (⚠️ otras páginas dicen 10 A / 125 VAC) | `0x0D` | **Bomba dosificadora de cloro / electroválvula** |
| **XN14** | Fuente 7–24 VDC → 3.3/5 V (jumper), fusible 1 A | — (sin I2C) | Alimentar el sistema sin USB |
| **XN44** | Buzzer piezo 31 Hz–20 kHz, 1 ms–60 s | `0x2C` | Alarma local |
| XN11 / XC03 / XN15 | 2 relevadores / LTE+GNSS / fuente 110–220 VAC | — | Aparecen en el índice del curso, sin contenido |

**El set del curso (5 zócalos):** XC01 + XN03 + XN13 + XN14 + XN44. Es muy probable que el kit de mañana sea ese o uno parecido.

### 2.5 Registros comunes a TODOS los X-NODE

| Reg | Nombre | R/W | Bytes | Descripción |
|---|---|---|---|---|
| `0x37` | STAT | R | 1 | `0x00` = sin error |
| `0x38` | FW | R | 3 | Versión (ej. 2.0.0) |
| `0x39` | UART_ID | R/W NV | 1 | Letra ‘A’–‘Z’ |
| `0x3A` | TW_ADD | R/W NV | 1 | Cambiar dirección I2C (1–126) — **NV = se guarda; cuidado** |
| `0x3B` | UART_EN | W | 1 | 1/0 habilita UART |
| `0x3C` | SLEEP | W | 1 | 1 = deep sleep |
| `0x3D` | RESET | W | 1 | 1 = reinicia el módulo |
| `0x3E` | **WHO_AM_I** | R | 2 | Byte 1 = modelo, byte 2 = revisión HW → **sirve para identificar qué módulo hay en cada dirección** |

Protocolo: `beginTransmission(addr) → write(reg) → [datos] → endTransmission()`; para leer: escribir el registro, `endTransmission()`, luego `requestFrom(addr, n)`.

### 2.6 Registros por módulo

**XN03 (0x03)** — entradas/salidas analógicas
| Reg | Nombre | R/W | Formato |
|---|---|---|---|
| `0x01`/`0x02`/`0x03` | O1_RED / O2_GREEN / O3_BLUE | R/W | 1–4 bytes. Escribir en `0x01` los bytes R, G, B (+ escala opcional) ⚠️ rango exacto no documentado (probar 0–100 y 0–255) |
| `0x10` | IN_POT | R | 0–100 (escalado) |
| `0x11`–`0x13` | IN1–IN3 | R | 0–100 (escalado) |
| `0x20` | ADC_POT | R | 0–1023 (10 bit, 2 bytes) |
| `0x21`–`0x23` | ADC1–ADC3 | R | 0–1023 → V = raw × 3.3 / 1023 |

UART: `XN03A+GP` → `XN03A=VAL` · `XN03A+S=R,G,B[,escala]`

**XN04 (0x04)** — T/H/Luz (2 bytes, MSB primero)
| Reg | Nombre | Conversión |
|---|---|---|
| `0x01` | TEMP | raw / 100 → °C |
| `0x02` | HUM | raw / 100 → %RH |
| `0x03` | LUM | lux directo |
| `0x04` | HEATER | 1/0 |
| `0x05` | PRECISION | 3 alta · 2 media · 1 baja |
| `0x06` | GAIN | 1, 2, 4, 8, 48, 96 |
| `0x36` | DRIVER | 1 activo (no tocar) |

Valores centinela: `0xFFFF` error · `0xFFFE` luz saturada · `0xFFFD` midiendo (esperar 20 ms) · `0xFFFC` driver apagado. **El ejemplo oficial no los revisa** (imprime 655.35 °C si hay error).

**XN13 (0x0D)** — relevador: reg `0x01` RELAY ← `0x01` encender / `0x00` apagar (también se puede leer).
UART: `XN13A+S=1` / `XN13A+S=0` (**con `\r\n`**; el ejemplo oficial lo omite y no compila).

**XN44 (0x2C)** — buzzer: reg `0x01` TONE ← 4 bytes `[freqH, freqL, durH, durL]` (freq 31–20000 Hz, dur 1–60000 ms). Reg `0x10` INT. UART: `XN44A+ST=freq,dur`.

**XN01 (0x01)** — reg `0x01` INPUT, 1 byte: bit0 = I1 … bit7 = I8. ⚠️ En UART (`XN01A+GS`) el orden es al revés (I1 a la izquierda).

**XN02 (0x02)** — reg `0x01` OUTPUT, 1 byte: bit0 = O1 … bit7 = O8. UART `XN02A+SO=O8…O1`.

**Todos por UART:** 115200 8N1, `Serial2.begin(115200, SERIAL_8N1, 9, 10)`, cada comando **termina en `\r\n`**, prefijo `XNnnA` (A = UART_ID por defecto).

---

## 3. Entorno de desarrollo (Arduino IDE)

1. Arduino IDE 2.x.
2. *Preferencias → URLs adicionales*:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   (la URL del curso, `https://dl.espressif.com/dl/package_esp32_index.json0`, trae una errata, el “0” final, y además apunta al índice antiguo).
3. *Gestor de tarjetas* → **esp32 by Espressif Systems** (instalado y probado: **4.0.0**; la placa venía compilada con 3.3.12).
4. *Herramientas*:

| Opción | Valor recomendado | Por qué |
|---|---|---|
| Placa | **ESP32S3 Dev Module** | |
| USB CDC On Boot | **Enabled** | Si no, `Serial.print` no sale por el USB |
| USB Mode | Hardware CDC and JTAG | |
| Upload Mode | UART0 / Hardware CDC | |
| Flash Size | **16MB (128Mb)** | Es lo que tiene la placa |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** | Más espacio para Blynk + Wi-Fi + lógica |
| PSRAM | **Disabled** | No tiene PSRAM |
| CPU Freq | 240 MHz | |

5. Librería: **Blynk** (by Volodymyr Shymanskyy) desde el *Library Manager*.

**Si no se reconoce la placa / falla la carga:** mantener **BOOT**, pulsar y soltar **RST**, soltar **BOOT** → cargar → pulsar **RST** para ejecutar.

### Desde terminal (alternativa, Linux/NixOS)

```bash
nix-shell -p arduino-cli esptool
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index && arduino-cli core install esp32:esp32
arduino-cli lib install Blynk
# Verifica nombres exactos de opciones con: arduino-cli board details -b esp32:esp32:esp32s3
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=disabled"
arduino-cli compile -b "$FQBN" sketch/ && arduino-cli upload -b "$FQBN" -p /dev/ttyACM0 sketch/
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

> **Hazlo HOY:** el core esp32 pesa cientos de MB. No dependas de la red del gimnasio mañana.

---

## 4. Código base

### 4.1 Funciones I2C robustas para X-NODE

```cpp
#include <Wire.h>
#define SDA_PIN 12
#define SCL_PIN 13

bool xnWrite(uint8_t addr, uint8_t reg, const uint8_t* data, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(data, n);
  return Wire.endTransmission() == 0;
}

bool xnRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// Lee 2 bytes MSB primero (en dos sentencias: el orden de evaluación de
// (Wire.read() << 8) | Wire.read() NO está garantizado en C++)
bool xnRead16(uint8_t addr, uint8_t reg, uint16_t& out) {
  uint8_t b[2];
  if (!xnRead(addr, reg, b, 2)) return false;
  out = (uint16_t(b[0]) << 8) | b[1];
  return true;
}

void setup() {
  Serial.begin(115200);
  Wire.setPins(SDA_PIN, SCL_PIN);
  Wire.begin();
}
```

### 4.2 Ejemplos de uso

```cpp
// Relevador XN13
bool relay(bool on) { uint8_t v = on; return xnWrite(0x0D, 0x01, &v, 1); }

// Buzzer XN44
bool tone44(uint16_t hz, uint16_t ms) {
  uint8_t d[4] = { uint8_t(hz >> 8), uint8_t(hz), uint8_t(ms >> 8), uint8_t(ms) };
  return xnWrite(0x2C, 0x01, d, 4);
}

// Entrada analógica XN03 (canal 1..3) en voltios; NAN si falla
float xn03Volts(uint8_t ch) {
  uint16_t raw;
  if (!xnRead16(0x03, 0x20 + ch, raw)) return NAN;
  return raw * 3.3f / 1023.0f;
}

// Potenciómetro XN03 (0..100) — ideal para SIMULAR un sensor en la demo
int xn03Pot() { uint8_t v; return xnRead(0x03, 0x10, &v, 1) ? v : -1; }

// Temperatura XN04 con validación de centinelas
float xn04Temp() {
  uint16_t raw;
  if (!xnRead16(0x04, 0x01, raw) || raw >= 0xFFFC) return NAN;
  return raw / 100.0f;
}
```

### 4.3 Escáner I2C (identifica qué módulos hay)

> Los escáneres típicos empiezan en `0x08`. **Los X-NODE usan 0x01–0x07**, así que hay que barrer desde 1.

```cpp
#include <Wire.h>
void setup() {
  Serial.begin(115200); delay(1500);
  Wire.setPins(12, 13); Wire.begin();
  Serial.println("Escaneando I2C 0x01-0x7E...");
  for (uint8_t a = 1; a < 0x7F; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      uint8_t who[2] = {0}, fw[3] = {0};
      Wire.beginTransmission(a); Wire.write(0x3E); Wire.endTransmission();
      if (Wire.requestFrom(a, (uint8_t)2) == 2) { who[0] = Wire.read(); who[1] = Wire.read(); }
      Wire.beginTransmission(a); Wire.write(0x38); Wire.endTransmission();
      if (Wire.requestFrom(a, (uint8_t)3) == 3) for (int i = 0; i < 3; i++) fw[i] = Wire.read();
      Serial.printf("0x%02X -> WHO_AM_I modelo=%u rev=%u  FW=%u.%u.%u\n",
                    a, who[0], who[1], fw[0], fw[1], fw[2]);
    }
  }
  Serial.println("Fin.");
}
void loop() {}
```

### 4.4 Blynk sin bloquear el control

El control de cloración **debe seguir funcionando sin Internet**. No uses `Blynk.begin()` (se bloquea si no hay Wi-Fi).

```cpp
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Cloracion ARCE"
#include "secrets.h"            // BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASS (en .gitignore)
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>    // los #define BLYNK_* van ANTES de este include

BlynkTimer timer;
float setpoint = 0.8;            // mg/L, ajustable desde la app

BLYNK_WRITE(V10) { setpoint = param.asFloat(); }      // slider en la app
BLYNK_CONNECTED() { Blynk.syncVirtual(V10); }

void enviar() {
  Blynk.virtualWrite(V0, /*cloro*/ 0.0);
  Blynk.virtualWrite(V1, /*pH*/ 7.0);
  // Blynk.logEvent("cloro_bajo", "Cloro residual < 0.2 mg/L");  // evento creado en la plantilla
}

void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Blynk.config(BLYNK_AUTH_TOKEN);
  timer.setInterval(2000L, enviar);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !Blynk.connected()) Blynk.connect(1000);
  Blynk.run();
  timer.run();
  // controlLocal();  // ← siempre se ejecuta, haya o no nube
}
```

---

## 5. Advertencias eléctricas

- **Lógica de 3.3 V.** Las entradas del XN03 aceptan **máx 3.3 V**. Muchos sensores de agua (turbidez, caudalímetros YF-S201) entregan **5 V**: usa un divisor resistivo (p. ej. 10 kΩ / 20 kΩ).
- **XN14:** alimenta **por jack O por clema, nunca ambos.** Entrada 7–24 VDC, revisar polaridad.
- **XN13:** si conmutas 127 VAC, trabaja con todo **desenergizado**, protege los bornes y pide supervisión. Para la demo es preferible una bomba de 12 VDC.
- **XN02:** máximo 20 mA por salida. Para cargas usa el relevador.
- No escribas `TW_ADD (0x3A)` ni `UART_ID (0x39)` por accidente: se guardan en EEPROM y cambian la dirección del módulo.
- **No quemar eFuses** (`DIS_USB_JTAG` desactiva el USB para siempre).
- Con 5 módulos + relevador, el **USB puede quedarse corto** (el relevador consume hasta 330 mA): el 14-nov la placa se desconectó sola. Para la demo, alimenta con XN14 o conecta directo al puerto del portátil, sin hub.

---

## 6. El problema técnico: cloración del agua

| Parámetro | Valor de referencia (NOM-127-SSA1-2021; verificar con el jurado) | Cómo medirlo |
|---|---|---|
| **Cloro residual libre** | **0.2 – 1.5 mg/L** | Sensor amperométrico (caro) · **ORP como indicador** · en la demo, el pot del XN03 |
| **pH** | **6.5 – 8.5** | Sonda pH + placa amplificadora analógica (0–3 V) → XN03 IN |
| **Turbiedad** | ≤ 4 UTN | Sensor óptico analógico (salida 0–4.5 V → **divisor**) |
| ORP | > 650 mV ≈ buena desinfección (referencia OMS) | Sonda ORP + amplificador analógico |
| Temperatura del agua | — | DS18B20 sumergible (1-Wire en GPIO libre, p. ej. INT = 7) |
| TDS / conductividad | — | Sensor TDS analógico |
| Caudal | — | Caudalímetro de pulsos (interrupción) o interruptor de flujo → XN01 |
| Nivel del tanque de hipoclorito | — | Flotador → XN01 |

**Química clave (para el pitch y la parte de “innovación”):** el cloro desinfecta como **ácido hipocloroso (HOCl)**, y la proporción HOCl/OCl⁻ depende del **pH** (pKa ≈ 7.5). Con pH 8.5 solo ~10 % del cloro libre es HOCl; con pH 6.5, ~90 %. **Dosificar sin mirar el pH es dosificar a ciegas.**

```
fracción_HOCl = 1 / (1 + 10^(pH − pKa))      pKa ≈ 7.5 (varía con la temperatura)
```

---

## 7. Propuestas para el reto

### Propuesta A (recomendada como base): **Clorador automático en lazo cerrado con *interlocks* de seguridad**
- **Mide:** cloro residual (pot del XN03 = sensor simulado en la demo; ORP/pH reales si vienen en el kit).
- **Actúa:** XN13 enciende la bomba dosificadora con **control proporcional por pulsos** (ventana de 30 s: % de tiempo encendido proporcional al error) + histéresis.
- **Seguridad (lo que distingue un sistema *inteligente*):**
  - No dosificar si no hay flujo (XN01 / interruptor de flujo).
  - Tiempo máximo de dosificación continua → alarma (bomba atascada o sensor dañado).
  - Sobrecloración > 1.5 mg/L → corte inmediato y alarma.
  - Tanque de hipoclorito bajo (flotador) → aviso.
- **Muestra:** LED RGB del XN03 como semáforo (verde = en norma, amarillo = corrigiendo, rojo = fuera de norma), buzzer XN44 en alarmas.

### Propuesta B: **Monitoreo multiparámetro + tablero Blynk + alertas**
- Dashboard con cloro, pH, turbidez, temperatura, estado de la bomba y modo (auto/manual).
- Setpoint y modo manual desde la app (`BLYNK_WRITE`).
- `Blynk.logEvent` para notificaciones push: cloro fuera de norma, falla de sensor, tanque bajo.
- Histórico para demostrar que el lazo estabiliza el cloro.

### Propuesta C (diferenciador de innovación): **Dosificación compensada por pH (“cloro efectivo”)**
- Calcular el **HOCl efectivo** = cloro libre × fracción_HOCl(pH, T) y controlar sobre esa variable, no sobre el cloro total.
- Mostrar en la app “Cloro libre” vs. “Cloro efectivo” y un **índice de calidad 0–100** combinado (cloro, pH, turbidez).
- Turbidez alta → aviso de que la desinfección pierde eficacia (protege a los microorganismos).

### Propuesta D (extra, si sobra tiempo): **Diagnóstico y mantenimiento predictivo**
- Detectar un **sensor congelado** (valor sin cambio en N minutos) o una deriva.
- Estimar el **consumo de hipoclorito** integrando el tiempo de bomba encendida → “días restantes de reactivo”.
- *Modo degradado:* si falla la nube o un sensor, dosificación fija segura + alarma local.

> **Estrategia:** A como núcleo funcional, B como interfaz, C (y algo de D) como “innovación” en el pitch. Todo cabe en el kit del curso (XC01 + XN03 + XN13 + XN14 + XN44), usando el **potenciómetro como sensor de cloro simulado** si no entregan sondas reales: así el jurado ve el lazo cerrado en vivo al girar la perilla.

---

## 8. Plan de las 6 horas (9:00–17:00)

| Hora | Objetivo | Entregable |
|---|---|---|
| 9:00–9:30 | Abrir el kit, foto de cada módulo, **escáner I2C**, cargar un *blink* | Lista de módulos + direcciones |
| 9:30–11:00 | Lectura de todos los sensores + relevador + buzzer | Monitor serie con datos reales |
| 11:00–12:30 | **Lazo de control A** + seguridad + semáforo RGB | Demo local funcionando **sin Internet** |
| 12:30–14:00 | Blynk: plantilla, datastreams, dashboard, eventos | App móvil en vivo |
| 14:00–15:30 | Innovación (C/D), pruebas de fallas, cableado limpio | Sistema estable |
| 15:30–16:30 | Pitch: problema → solución → demo en vivo → impacto → escalabilidad | Presentación |
| 16:30–17:00 | Ensayo y respaldo (video de la demo por si algo falla) | — |

**Regla de oro:** guarda en Git una versión que funcione cada hora (`git commit`), así siempre tienes algo que demostrar.

---

## 9. Checklist para llevar mañana

- [ ] Portátil con el **core esp32 y la librería Blynk instalados** (probados hoy compilando un sketch)
- [ ] Cable USB-C **de datos** (y uno de repuesto)
- [ ] Cuenta Blynk creada; plantilla y datastreams preparados desde hoy
- [ ] Hotspot del celular como Wi-Fi de respaldo (2.4 GHz)
- [ ] Este archivo + los sketches base (escáner, funciones I2C, Blynk) en el repo
- [ ] Multímetro, jumpers dupont, divisores resistivos (10k/20k)
- [ ] Opcional: una bomba de 12 V, un vaso de agua y una sonda de pH/ORP para la demo física

---

## 10. Erratas conocidas de la documentación oficial

1. URL del gestor de tarjetas termina en `json0` (errata).
2. XC01 dice BT 4.2 BR/EDR; el ESP32-S3 solo tiene BLE 5.
3. Ejemplo XN03: el comentario dice registro `0x1E`, el código usa `0x10` (correcto `0x10`).
4. Ejemplo XN04: no valida centinelas; `(read()<<8)|read()` con orden indefinido.
5. Ejemplo UART XN13: sin `\r\n`, usa `printBinary` sin definir y duplica `String success` → no compila.
6. XN01/XN02: el orden de bits entre UART e I2C es opuesto.
7. XN02: la página da 20 mA y 40 mA como límite → usar 20 mA.
8. XN13: 8 A / 250 VAC (R2) vs 10 A / 125 VAC (R1).
9. XN14: el calibre de cable para la clema es AWG 18–26 (curso) vs 20–24 (ficha).
10. Instrucción de recuperación del XC01: “soltar RST” aparece dos veces; el último paso es soltar **BOOT**.
