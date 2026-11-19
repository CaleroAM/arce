# ARCE · Control de pH IoT — Hackatón Puebla 2026

Equipo **ARCE** (Instituto Tecnológico de Puebla) · Reto: [*Monitoreo y control de calidad de agua IoT*](https://docs.microside.com/workshops/hackathon/reto-monitoreo-y-control-de-calidad-de-agua-iot) · Kit XIDE (MICROSIDE)

Sistema que mide el pH del agua, dosifica una solución de pH 4 con una bomba hasta un umbral definido por el usuario (4.5–7), **sin pasarse**, y se supervisa desde Blynk.Cloud y desde un dashboard propio servido por el ESP32.

> 📘 **Documentación completa para la presentación:** [`DOCUMENTACION.md`](DOCUMENTACION.md) (arquitectura, algoritmo, pruebas, problemas resueltos, guion y preguntas del jurado)
> Pines y registros verificados: [`PINES.md`](PINES.md) · Referencia del kit: [`XIDE_DISPOSITIVO.md`](XIDE_DISPOSITIVO.md)

---

## Estado actual (19 nov 2026, ~12:45)

| Elemento | Estado |
|---|---|
| ESP32-S3 (XC01, MAC …1B:10) | ✅ Con `control_ph` cargado · firmware original respaldado en `backup/` |
| XN03 (sonda / potenciómetro / LED RGB) en `0x03` | ✅ Responde (FW 2.0.2) · LED con el indicador universal |
| XN44 (buzzer) en `0x2C` | ✅ Responde (FW 2.1.0) |
| XN13 (relevador) en `0x0D` | ⚠️ Responde a su dirección y a las escrituras · sus lecturas devuelven 0xFF |
| Sonda de pH (XN03 IN1) | ✅ Calibrada: ADC 607 = pH 6.95 · lectura 6.93–6.97 |
| Bus I2C | ✅ 2704/2704 lecturas correctas (100 %) |
| Wi-Fi propio `ARCE-prueba` + página local + OTA `/update` | ✅ Verificado (OTA en 7 s) |
| Blynk (plantilla BomACER, NY3) | ✅ Online · 10 datastreams · panel web con 8 widgets |
| **Dosificación con agua real** | ⏳ Pendiente |

---

## Estructura del proyecto

```
arce/
├── README.md                     ← este archivo
├── XIDE_DISPOSITIVO.md           ← guía técnica del kit y del reto
├── prueba_kit/                   ← prueba sin cable: LED, buzzer y página Wi-Fi (192.168.4.1)
├── sketches/
│   ├── control_ph/               ← ⭐ firmware del reto (control de pH + Blynk)
│   ├── escaner_i2c/              ← detecta módulos y su WHO_AM_I
│   ├── led_morado/               ← prueba del LED RGB del XN03
│   ├── led_morado_esp32/         ← prueba del LED de la XC01 (GPIO16)
│   └── clorador_arce/            ← versión anterior (cloro), solo referencia
├── backup/                       ← volcados de flash + SHA256SUMS (no subir a git)
└── .claude/skills/xide-hackathon ← skill para Claude Code
```

---

## Requisitos del reto y cómo se cumplen

| # | Requisito oficial | Implementación en `control_ph` |
|---|---|---|
| 1 | Convertir la salida analógica de la sonda a pH, con filtro contra el ruido | Media recortada de 25 muestras + suavizado EMA · calibración en 2 puntos (`cal7` / `cal4`) |
| 2 | Activación remota; impedirla si el pH ya está bajo el umbral | `solicitarInicio()` rechaza con doble tono grave si pH ≤ umbral |
| 3 | Regular el tiempo de encendido y apagar al alcanzar el umbral | Control **"pulsa y espera" adaptativo**: aprende pH/ms de bomba y nunca apunta a pasarse |
| 4 | Dos alertas sonoras distintas (inicio / fin) | Melodía ascendente al iniciar, descendente al terminar (XN44) |
| 5 | Indicador universal de colores | LED RGB del XN03 + color en el dashboard |
| 6 | **Dashboard en Blynk.Cloud** (obligatorio) | Datastreams V0–V9 + eventos (ver `sketches/control_ph/secrets.h.example`) |

> ⚠️ **Blynk es obligatorio** (requisito 6). El dashboard propio es un **valor añadido**, no un sustituto.

---

## Plan de valor añadido

### Idea central
**Blynk como mínimo obligatorio + dashboard propio en el ESP32.**
Argumento para el jurado: *"si se cae la nube, la planta sigue operando y supervisándose"*. El sistema no depende de ninguna plataforma externa para funcionar.

### Propuestas

#### 1. Carga de programas por Wi-Fi (OTA) — necesaria hoy
- El cable USB no cabe con los módulos montados; cada cambio obliga a desmontar.
- Con OTA el portátil sube el código al ESP32 por Wi-Fi, con todo montado.
- Es una función que tienen los equipos reales en campo.
- **Requisito:** una última carga por USB con la versión que ya acepta OTA.

#### 2. Dashboard propio en el ESP32
- Gráfica en vivo del pH con la línea del umbral y una marca en cada pulso de bomba.
- Controles: umbral, iniciar/detener dosificación, calibración — con los mismos bloqueos de seguridad del firmware.
- Indicador con el color universal, estado de la bomba, mL gastados.
- Funciona desde el celular sin instalar nada y **sin Internet**.

#### 3. Métricas de precisión
Gana quien "se acerque más al umbral": hay que **mostrarlo con números**. Al terminar cada dosificación:
- pH final y **error respecto al umbral**
- mL de solución usados
- número de pulsos y tiempo total

#### 4. Registro de datos y curva de titulación
- Guardar cada sesión en la memoria del ESP32 (partición de datos de 9.9 MB libre) y descargarla en CSV.
- Graficar **pH vs. mL añadidos**: la curva que aprende el algoritmo.
- Da rigor científico y material para responder las preguntas del jurado.

#### 5. Control y comunicaciones en núcleos separados (FreeRTOS)
- El ESP32-S3 tiene dos núcleos: lazo de control en uno, Wi-Fi + dashboards en otro.
- Una desconexión o una página lenta nunca retrasa el apagado de la bomba.
- La página del reto dedica una sección entera a FreeRTOS.

#### 6. Conexión con el tema del evento (pitch, sin código)
- El hackatón es sobre **cloración**: el cloro desinfecta como HOCl, y su proporción depende del pH
  (pH 6.5 → ~90 % HOCl · pH 8.5 → ~10 %). **Controlar el pH es lo que hace eficaz la cloración.**
- El mismo algoritmo "pulsa y espera" sirve para dosificar hipoclorito → escalable a Agua de Puebla.

### Prioridades

| Prioridad | Tarea | Responsable | Tiempo aprox. |
|---|---|---|---|
| 1 | OTA + dashboard propio integrado en `control_ph` | Firmware (con Claude) | ~1 h |
| 2 | Métricas de precisión + registro de datos (CSV) | Firmware (con Claude) | ~30 min |
| 3 | Plantilla de Blynk: datastreams V0–V9, eventos, dashboard web y móvil | Equipo (app) | ~20 min |
| 4 | Separar control y Wi-Fi en dos núcleos (FreeRTOS) | Firmware (con Claude) | ~20 min |
| — | Pitch: pH → eficacia del cloro → escalable a Agua de Puebla | Equipo | en paralelo |

### Reparto sugerido por el propio reto
- **Maqueta:** montar XN13, sonda, bomba y fuente según los diagramas.
- **Firmware:** `control_ph` + OTA + dashboard propio.
- **App / nube:** plantilla y dashboard de Blynk (el reto recomienda el *widget HTML*).

---

## Pasos inmediatos con el hardware

1. Montar el **XN13** (sin bomba basta para oír el "clic").
2. Conectar la **sonda**: Po → XN03 IN1 · V+ → 3V3 · G → GND. Con la sonda en el agua, el ADC debe rondar **511** (pH 7).
3. Cargar `control_ph` (por USB la primera vez).
4. Calibrar: sonda en el agua → `cal7`; sonda en la solución pH 4 → `cal4`.
5. Cebar la manguera: `cebar 200`.
6. Primera dosificación de prueba con umbral cercano (p. ej. **6.5**) para ver cuánto baja el pH por pulso.

> Solo hay **100 mL de solución pH 4** (≈ 3 s de bomba en total). No gastarla en pruebas sin el control.

---

## Comandos

Todos desde la carpeta del proyecto, en NixOS:

```bash
cd /home/calero/Documentos/docker/PROYECTOS/arce
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=disabled"

# Compilar y subir (cambia la carpeta por el sketch que quieras)
nix-shell -p arduino-cli --run "arduino-cli compile -u -b $FQBN -p /dev/ttyACM0 sketches/control_ph"

# Monitor serie (salir con Ctrl+C; ciérralo antes de volver a subir)
nix-shell -p arduino-cli --run 'arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200'

# Restaurar el firmware original de la placa
nix-shell -p esptool --run 'esptool --port /dev/ttyACM0 write-flash 0 backup/flash_431B10_20261119_1006.bin'
```

**Comandos serie de `control_ph`:** `start` · `stop` · `umbral 5.5` · `cal7` · `cal4` · `calreset` · `cebar 300` · `info`

**Si falla la carga:** cerrar el monitor serie · mantener **BOOT**, pulsar y soltar **RST**, soltar **BOOT** · volver a subir · pulsar **RST**.

**Sin cable:** conectar el celular a la red **`ARCE-prueba`** (clave `arce2026`) y abrir **http://192.168.4.1**.

---

## Seguridad

- Secretos (token de Blynk, clave Wi-Fi) solo en `secrets.h`, que está en `.gitignore`.
- La bomba nunca depende de la nube: el control es local y el relevador se fuerza a apagado fuera de los pulsos.
- Fuente de 12 V: alimentar la XN14 por jack **o** por clema, nunca ambos.
- La solución de pH 4 es ácida: manipular con cuidado y lejos de la electrónica.
