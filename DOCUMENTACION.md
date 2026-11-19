# Control inteligente de pH IoT — Documentación del proyecto

**Equipo ARCE** · Instituto Tecnológico de Puebla · **Hackatón Puebla 2026 “Agua inteligente”** (19 nov 2026)
Reto oficial: [*Monitoreo y control de calidad de agua IoT*](https://docs.microside.com/workshops/hackathon/reto-monitoreo-y-control-de-calidad-de-agua-iot) · Plataforma: kit **XIDE** de MICROSIDE

> Documento base para preparar la presentación y responder las preguntas del jurado.
> Detalle de pines y registros: [`PINES.md`](PINES.md) · Referencia del kit: [`XIDE_DISPOSITIVO.md`](XIDE_DISPOSITIVO.md) · Arranque rápido: [`README.md`](README.md)

---

## 1. Resumen ejecutivo

**Problema.** En el tratamiento de agua, la dosificación de químicos (reguladores de pH, hipoclorito) suele hacerse a ojo o con temporizadores fijos. Eso provoca sobredosis o subdosis, desperdicio de reactivo y agua fuera de norma. Además, el pH determina si el cloro desinfecta: el cloro actúa como ácido hipocloroso (HOCl), y su proporción cae del ~90 % a pH 6.5 al ~10 % a pH 8.5.

**Solución.** Un sistema IoT que:
1. **mide** el pH con una sonda analógica, filtrando el ruido eléctrico;
2. **dosifica** solución reguladora (pH 4) con una bomba, mediante un algoritmo adaptativo que **aprende** cuánto baja el pH cada pulso y **se detiene en el umbral sin pasarse**;
3. **informa y alerta**: LED con el color del indicador universal de pH, sonidos distintos al iniciar y al terminar, página web local y **dashboard en Blynk.Cloud**.

**Diferenciadores.**
- Control **local y seguro**: funciona aunque se caiga Internet. La nube supervisa, pero no controla.
- Algoritmo **"pulsa y espera" adaptativo**, pensado para el retardo de mezcla y el comportamiento químico real.
- **Varias capas de seguridad**: bloqueo por umbral, límite de solución, detección de bomba sin efecto, relevador forzado a apagado.
- **Actualización de firmware por Wi-Fi (OTA)** y página de operación local sin Internet.
- **Escalable** a la cloración real de Agua de Puebla: el mismo algoritmo sirve para dosificar hipoclorito.

---

## 2. Requisitos del reto y cumplimiento

| # | Requisito oficial | Cómo lo cumplimos | Estado |
|---|---|---|---|
| 1 | Convertir la señal analógica de la sonda a pH, **filtrar el ruido eléctrico** y **enviar el valor a la nube** | Muestreo a 50 Hz, media recortada de 25 muestras y suavizado EMA. Calibración por software. Envío a Blynk V0 cada 1 s | ✅ Verificado: pH 6.93–6.97 estable (±0.01) y llegando a Blynk |
| 2 | El usuario define el **umbral de apagado** entre 7 y 4.5 | Slider en Blynk (V1), formulario en la página local y comando serie. Se guarda en memoria no volátil | ✅ Verificado en Blynk |
| 3.1 | **Activación remota**; si el pH ya está **por debajo del umbral**, impedir que se active la bomba | `solicitarInicio()` rechaza la orden, suena un doble tono grave y el interruptor vuelve a 0 | ✅ Programado · ⏳ sin probar con agua |
| 3.2 | **Regular el tiempo de encendido** para alcanzar el umbral | Pulsos de 50–600 ms calculados con la ganancia aprendida | ✅ Programado · ⏳ sin probar con agua |
| 3.3 | **Apagar automáticamente** la bomba al alcanzar el umbral | Se detiene cuando pH ≤ umbral + 0.05, o cuando el siguiente pulso mínimo se pasaría del umbral | ✅ Programado · ⏳ sin probar con agua |
| — | Tener en cuenta la **propagación** de la solución | Espera de mezcla de 6–30 s y detector de estabilidad (< 0.01 pH/s) antes de decidir | ✅ Programado |
| — | "Mejor sistema el que se acerque más al umbral" | El pulso apunta al 60 % del error restante y nunca por debajo del umbral | ✅ Programado |
| 4 | Dos **alertas sonoras distintas** (inicio y fin) | XN44: melodía ascendente al iniciar y descendente al terminar (más doble tono grave al rechazar) | ✅ Programado · ❓ falta confirmar que se oyen |
| 5 | **Indicador universal de colores** | LED RGB del XN03 con la tabla del reto (0 rojo → 7 verde → 14 violeta). El mismo color en la web y en Blynk | ✅ Verificado: verde a pH 7 |
| 6 | **Dashboard en Blynk.Cloud**: ver el pH, definir el umbral y activar la bomba a distancia | Plantilla BomACER, 10 datastreams y panel web con 8 widgets | ✅ Verificado: dispositivo Online con datos en vivo |

---

## 3. Arquitectura

```
                 ┌──────────────────────── Blynk.Cloud (NY3) ─────────────────────────┐
                 │  Panel web / app: pH, umbral, dosificar, estado, bomba, mL, gráfica │
                 └───────────────▲─────────────────────────────────────────────────────┘
                                 │ Wi-Fi "hackathon2026" (estación, con Internet)
 Celular / portátil ──Wi-Fi──►┌──┴───────────────────────────────┐
 "ARCE-prueba"                │  ESP32-S3 (XC01)                 │
 http://192.168.4.1           │  • adquisición + filtro          │
 (estado, control, /update)   │  • máquina de estados de dosis   │
                              │  • seguridad                     │
                              │  • servidor web + OTA + Blynk    │
                              └──┬───────────────────────────────┘
                                 │ I2C (SDA GPIO12 / SCL GPIO13, 100 kHz)
          ┌──────────────────────┼──────────────────────┬──────────────────────┐
     XN03 (0x03)            XN13 (0x0D)            XN44 (0x2C)            XN14 (fuente)
     sonda pH (IN1)         relevador              buzzer                 12 V → 3.3/5 V
     temp. sonda (IN2)      └─► bomba 12 V
     potenciómetro          (diafragma, 33 mL/s)
     LED RGB
```

**Por qué esta arquitectura:**
- **El ESP32 no toca la parte eléctrica.** Cada X-NODE tiene su propio microcontrolador y se comunica por I2C. Así el control queda aislado de los 12 V de la bomba.
- **Wi-Fi en modo doble (AP + STA):** la red propia `ARCE-prueba` siempre está disponible, para la operación local y la actualización de firmware. La conexión a Internet se usa solo para Blynk.
- **El control nunca depende de la nube.** Si Blynk o Internet fallan, la dosificación sigue funcionando y se puede operar desde la página local o con el botón BOOT.

---

## 4. Hardware

| Componente | Modelo | Función | Verificado |
|---|---|---|---|
| Controlador | **XC01 R5** — ESP32-S3 (QFN56, rev 0.2), 240 MHz doble núcleo, **16 MB de flash, sin PSRAM**, Wi-Fi y BLE 5 | Cerebro del sistema | ✅ esptool |
| Placa base | **XBI01 IIoTrainer** | 5 zócalos mikroBUS con el bus I2C compartido | ✅ |
| E/S analógicas | **XN03** R2, FW 2.0.2 | ADC de 10 bits para la sonda, potenciómetro, LED RGB | ✅ WHO_AM_I = 3 |
| Relevador | **XN13 R1** — Songle SRD-05VDC-SL-C, **firmware 0.1** | Enciende y apaga la bomba | ✅ **Por UART** (`XN13A+S=1/0` → `OK`) |
| Buzzer | **XN44** R1, FW 2.1.0 | Alertas sonoras | ✅ WHO_AM_I = 44 |
| Fuente | **XN14** | 12 V → 3.3 / 5 V | ✅ |
| Sonda | PH-4502C con tarjeta analógica | Po (pH) → IN1 · To (temperatura) → IN2 | ✅ |
| Bomba | Diafragma de 12 V y 3 W, **33 mL/s** | Dosifica la solución de pH 4 | Pendiente de probar |
| Fuente externa | 12 V, 12 W | Alimenta la XN14 y la bomba | ✅ |

**Conexiones** (diagramas oficiales):
- Sonda: Po → XN03 **IN1**, To → **IN2**, V+ → **3V3**, G → **GND**.
- Bomba: fuente (+) → XN13 **COM**, **NA** → bomba (+), bomba (−) → fuente (−).

**Montaje hidráulico recomendado:**
- La succión va en el **fondo** del contenedor de solución.
- La salida va **lejos de la sonda** y **por encima del agua**, goteando.
- El contenedor de solución debe estar **a la misma altura o más bajo** que el del agua, para evitar el efecto sifón.

---

## 5. Software

### 5.1 Firmware: `sketches/control_ph/control_ph.ino`
Arduino, core esp32 4.0.0. Ocupa el 33 % de la flash y el 15 % de la RAM.

| Módulo | Funciones principales |
|---|---|
| Adquisición y filtro | `muestrear()`, `mediaRecortada()`, `calcularPh()` |
| Calibración | `adcAPh()` y comandos `cal7` / `cal4` / `calreset`, guardados en NVS (`Preferences`) |
| Control | `solicitarInicio()`, `siguientePulso()`, `actualizarControl()`, `finalizar()` |
| Actuadores | `bombaPulso()`, `bombaApagar()`, `setRele()` |
| Señalización | `colorDePh()` + `actualizarLed()` (LED RGB), `tocar()` (melodías sin bloqueo) |
| Conectividad | Página local `/`, `/cmd`, actualización `/update`, Blynk (`BLYNK_WRITE` V1 y V2, `enviarBlynk()`) |

### 5.2 Medición y filtro de ruido (requisito 1)
1. **Muestreo:** cada 20 ms (50 Hz) se lee el ADC de IN1 del XN03 (10 bits, 0–1023, 3.3 V). Las lecturas inválidas se descartan.
2. **Media recortada:** cada 250 ms se ordenan las últimas **25 muestras** (0.5 s), se **descartan el 25 % más alto y el 25 % más bajo** y se promedia el 50 % central. Esto elimina los picos de interferencia de la bomba, el relevador y el Wi-Fi.
3. **Suavizado exponencial (EMA):** `filtrado = 0.7 · anterior + 0.3 · nuevo`, que elimina el ruido fino.
4. **Conversión:** `pH = 7 + (ADC − ADC₇) · (−3) / (ADC₄ − ADC₇)`.
5. **Estabilidad:** se guarda el historial del pH (1 muestra por segundo) y se calcula la pendiente en los últimos 4 s. El control solo decide si la pendiente es menor de **0.01 pH/s**.

**Resultado medido:** ADC 606.3–606.6, es decir, **±0.01 pH** de variación en reposo.

### 5.3 Calibración
- **La tabla del fabricante** da ADC 511 para pH 7 y 674 para pH 4 (pendiente de 54.33 ADC/pH).
- **La sonda real** da **ADC 607 en agua de pH 6.90–7.0**: unas 96 unidades más que la tabla. Sin calibrar, el agua neutra se leería como pH 5.2.
- **Calibración aplicada**, de un punto con la pendiente de fábrica: **ADC 607 = pH 6.95**, lo que da **ADC₇ = 604.3** y **ADC₄ = 767.3**.
- **Pendiente de confirmar:** hay que medir la solución de pH 4. Debería dar un ADC de ≈767. Si diera ≈800, la causa sería la escala del ADC y no un desplazamiento, y se corregiría con `cal4`.

### 5.4 Algoritmo de dosificación "pulsa y espera" adaptativo (requisito 3)

**Restricciones físicas:**
- La bomba da **33 mL/s**, y el relevador conmuta en un mínimo de **50 ms** (≈1.65 mL).
- Solo hay **100 mL** de solución, es decir, ≈3 s de bomba en total.
- La solución **tarda en mezclarse**, y mientras tanto la lectura cambia rápido.
- No hay base para corregir: **pasarse del umbral no tiene vuelta atrás**.

**Máquina de estados:**
```
            orden "Dosificar" (Blynk V2, web, BOOT)
                       │
          ¿pH ≤ umbral + 0.05?  ──sí──► RECHAZO (doble tono grave, V2 = 0)
                       │ no
                       ▼
   ┌─────────► DOSIFICANDO: pulso de N ms (relevador encendido)
   │                   │ fin del pulso
   │                   ▼
   │           MEZCLANDO: espera ≥ 6 s y lectura estable (< 0.01 pH/s), máx. 30 s
   │                   │
   │      mide ΔpH → actualiza la ganancia (pH/ms)
   │                   │
   │      ¿pH ≤ umbral + 0.05? ──sí──► FIN "Umbral alcanzado" (melodía de fin, V2 = 0)
   │      ¿el pulso mínimo se pasaría? ──sí──► FIN "Lo más cerca posible sin pasarse"
   │      ¿5 pulsos sin efecto? ──sí──► FIN con error (manguera vacía o sin solución)
   │      ¿≥ 2.5 s de bomba (≈82 mL)? ──sí──► FIN "Límite de solución"
   │                   │ no
   └─── siguiente pulso = 0.6 · (pH − umbral) / ganancia   (50–600 ms)
```

**Cómo aprende:**
- **Primer pulso:** 100 ms, porque todavía no conoce la ganancia. Si no hay efecto, prueba 200 y luego 400 ms; así se ceba la manguera.
- **Después:** `ganancia = ΔpH / ms_de_bomba`, promediada al 50 % con la estimación anterior.
- **Siguiente pulso:** corrige solo el **60 %** del error restante, así que el sistema se acerca al umbral de forma asintótica y nunca lo cruza a propósito.

**Por qué es seguro desde el punto de vista químico:** al acercarse a pH 4, que es el pH de la solución, cada mL baja el pH **cada vez menos**. La ganancia aprendida en el pulso anterior es entonces **mayor** que la real, así que el siguiente pulso sale **más corto** de lo necesario. El error siempre cae del lado seguro.

### 5.5 Seguridad
| Protección | Implementación |
|---|---|
| Estado seguro al arrancar | Relevador en apagado dentro de `setup()` |
| Relevador forzado | Fuera de un pulso, se escribe "apagado" cada 2 s, y se reintenta si falla el I2C |
| Bloqueo por umbral | No se activa si pH ≤ umbral (requisito 3.1) |
| Límite de solución | Máximo 2.5 s de bomba por sesión (≈82 de 100 mL) |
| Bomba sin efecto | 5 pulsos sin cambio de pH → paro (protege la bomba de funcionar en seco) |
| Sensor inválido | Sin lectura válida → no arranca, o detiene la dosificación |
| Actualización de firmware | Durante la OTA se detiene la dosificación, se apaga el relevador y el LED se pone azul |
| Nube opcional | Los intentos de conexión a Blynk solo se hacen con la bomba parada (3 s como máximo) |

### 5.6 Interfaces de usuario
| Interfaz | Acceso | Funciones |
|---|---|---|
| **Blynk.Cloud** (panel web) | blynk.cloud → dispositivo BomACER | Indicador de pH con color, slider de umbral, interruptor Dosificar, estado, LED de bomba, mensaje, mL y gráfica del pH y el umbral |
| **Página local** (sin Internet) | Wi-Fi `ARCE-prueba` → http://192.168.4.1 | pH con color, estado, bomba, mL, ganancia, consumo, estado de la conexión, Iniciar/Detener, umbral y enlace a la actualización |
| **Físicas** | En la maqueta | LED RGB con el indicador universal, melodías del buzzer, botón BOOT (iniciar/detener) |
| **Serie** (USB) | 115200 baudios | CSV continuo y comandos `start`, `stop`, `umbral X`, `cal7`, `cal4`, `calreset`, `cebar MS`, `info` |

### 5.6.1 Indicadores físicos: qué significa cada luz y cada sonido

**LED RGB del XN03** (el módulo del potenciómetro) — **indicador universal de pH**, se actualiza cada 0.5 s:

| pH | Color | Carácter | | pH | Color | Carácter |
|---|---|---|---|---|---|---|
| 0 | 🟥 Rojo intenso | Ácido fuerte | | 8 | 🟦 Verde azulado | Alcalino débil |
| 1–2 | 🟥 Rojo | Ácido fuerte | | 9 | 🟦 Azul | Alcalino débil |
| 3 | 🟧 Rojo-naranja | Ácido | | 10 | 🟦 Azul oscuro | Alcalino |
| 4 | 🟧 Naranja | Ácido | | 11 | 🟪 Índigo | Alcalino |
| 5 | 🟨 Amarillo-naranja | Ácido débil | | 12–13 | 🟪 Violeta | Alcalino fuerte |
| 6 | 🟨 Amarillo | Ácido débil | | 14 | 🟪 Violeta oscuro | Alcalino fuerte |
| **7** | 🟩 **Verde** | **Neutro** | | | | |

Entre dos valores enteros el color se interpola, por ejemplo 6.5 es amarillo verdoso. **Durante la dosificación** de 7 → 4.5 el LED pasa de **verde → amarillo → naranja**.

| Situación especial | LED del XN03 |
|---|---|
| Actualización de firmware en curso | 🟦 **Azul fijo** (y el relevador forzado a apagado: no suena) |
| Prueba de colores (`/test?colores=1`) | Recorre todo el indicador, de pH 0 a 14, en 15 s |
| Sonda sin lectura válida | Apagado |

**Registros (verificado leyendo de vuelta):** `0x01` = R, `0x02` = G, `0x03` = B, con valores de **0–255** (verificado). Escribir 3 bytes en `0x01` rellena los tres canales. Un 4.º byte opcional regula el brillo de los tres; sin él, el brillo es el máximo. `LED_MAX = 255` (brillo máximo).

**LED de la placa XC01 (GPIO16):** **se enciende mientras la bomba está activa**, durante cada pulso.

**Buzzer XN44** (verificado: el registro TONE guarda exactamente la frecuencia y la duración enviadas):

| Evento | Sonido | Frecuencias |
|---|---|---|
| Arranque de la placa | 1 pitido corto | 2700 Hz, 100 ms |
| **Inicio de la dosificación** | Melodía **ascendente** de 3 notas | 2000 → 2700 → 3500 Hz |
| **Fin de la dosificación** (umbral alcanzado, cancelado o error) | Melodía **descendente** de 3 notas | 3500 → 2700 → 2000 Hz |
| **Activación rechazada** (pH ≤ umbral o sonda inválida) | **Dos tonos** más graves | 1200 Hz × 2 |

> ⚠️ El piezo suena mucho más fuerte hacia 2.5–4 kHz. En un ambiente ruidoso, como el gimnasio, conviene subir las melodías a ese rango.

**Dashboards:**

| Indicador | Blynk | Dashboard local |
|---|---|---|
| pH con su color | Indicador V0 (el color lo cambia el firmware) | Número grande sobre el color del indicador universal y escala 0–14 con marcadores |
| Bomba | LED V5 (encendido = pulso activo) | Punto animado mientras bombea; franja sombreada en la gráfica |
| Estado | Etiqueta V3: REPOSO / DOSIFICANDO / MEZCLANDO | Igual |
| Resultado | Etiqueta V8, con el error respecto al umbral | Tarjeta "Última dosificación": verde si el error es ±0.1, ámbar si es mayor, rojo si quedó por debajo del umbral |

### 5.7 Blynk.Cloud
**Plantilla y región:** `BomACER` (`TMPL2XEggRshL`), región NY3, dispositivo **BomACER**. El token está en `sketches/control_ph/secrets.h`, que está **excluido de git**.

| Pin | Datastream | Tipo | Rango | Sentido |
|---|---|---|---|---|
| V0 | pH | Doble | 0–14 | Dispositivo → nube (el color lo cambia el firmware con `setProperty`) |
| V1 | Umbral | Doble | 4.5–7 | App ↔ dispositivo |
| V2 | Dosificar | Entero | 0–1 | App → dispositivo (el firmware lo devuelve a 0 al terminar) |
| V3 | Estado | Cadena | | REPOSO / DOSIFICANDO / MEZCLANDO |
| V4 | Voltaje | Doble | 0–3.3 | Voltaje de la sonda |
| V5 | Bomba | Entero | 0–1 | Estado del relevador |
| V6 | Dosis_mL | Doble | 0–100 | mL dosificados en la sesión |
| V7 | Color | Cadena | | Color `#RRGGBB` del indicador |
| V8 | Mensaje | Cadena | | Resultado o último evento |
| V9 | Temp_V | Doble | 0–3.3 | Salida To de la sonda, sin calibrar |

### 5.8 Actualización de firmware por Wi-Fi (OTA)
- **Por qué hacía falta:** con los módulos montados, el cable USB choca con ellos. Cada cambio de código obligaba a desmontar.
- **Primer intento:** `ArduinoOTA` + `espota`. Lo bloqueaba el firewall de la portátil, porque el ESP32 tiene que abrir una conexión hacia ella.
- **Solución adoptada:** **OTA por HTTP**. El ESP32 recibe el firmware en `POST /update`, con autenticación básica. Funciona desde la terminal (`./subir_wifi.sh`) y desde el navegador del celular.
- **Resultado:** 1 MB en **7 segundos**, con reinicio verificado.

### 5.9 Dashboard propio (servido por el ESP32, sin Internet)
- **Acceso:** Wi-Fi `ARCE-prueba` → **http://192.168.4.1**. La página anterior, más sencilla, sigue en `/simple`.
- **Tecnología:** HTML, CSS y JS sin librerías (`sketches/control_ph/web_ui.h`, 14.8 KB). Consulta `/api/estado` cada 1 s.
- **Contenido:**
  - pH grande con el color universal y escala 0–14;
  - **gráfica de 5 min** con la línea del umbral y los pulsos de la bomba sombreados;
  - tarjetas de mL, pulsos, ganancia y consumo;
  - estado del Wi-Fi y de Blynk;
  - botones **Iniciar / Detener** y campo de **umbral**;
  - tarjeta **"Última dosificación"** (pH inicial y final, **error**, mL, pulsos y duración);
  - panel de **pruebas**.
- **Diseño:** pensado primero para el celular, con modo claro u oscuro automático y aviso de "sin conexión".
- **API JSON:** `GET /api/estado` devuelve pH, ADC, voltaje, umbral, estado, bomba, mL de la sesión, pulsos, ganancia, consumo total, mensaje, color, Wi-Fi, Blynk, tiempo encendido y `ultima_sesion` (pH inicial y final, umbral, error, mL, pulsos, duración, motivo y éxito).

### 5.9.1 Dashboard remoto público (Tailscale Funnel)
- **URL pública:** **https://acer.tail2b1858.ts.net:8443**. Cualquiera puede abrirla desde cualquier red, sin instalar nada.
- **Funcionamiento:** ESP32 → Blynk.Cloud → `remoto/puente_blynk.py` en la portátil (127.0.0.1:8090) → Tailscale Funnel (HTTPS).
- **Mismo dashboard** que el del ESP32, con datos de Blynk cada ~2 s (una sola petición por ciclo, compartida por todos los visitantes).
- **Solo lectura:** se ocultan los paneles de Control, Módulos y Pruebas, y `/cmd`, `/test`, `/diag` y `/update` responden 403. Nadie de fuera puede encender la bomba. El token de Blynk se queda en la portátil.
- **Limitaciones:**
  - depende de que la portátil esté encendida, con Internet y con el puente en marcha;
  - mientras la portátil se conecta a `ARCE-prueba` para subir firmware (~1 min), la vista remota se corta;
  - algunos datos solo existen en el ESP32 (pulsos, ganancia, versiones de los módulos) y en la vista remota salen como "--".
- **Arrancar:** `python3 -I remoto/puente_blynk.py` · **publicar:** `tailscale funnel --bg --https=8443 http://127.0.0.1:8090` · **retirar:** `tailscale funnel --https=8443 off`

### 5.10 Rutas de prueba y diagnóstico (solo en REPOSO)
| Ruta | Qué hace |
|---|---|
| `/test?sonido=inicio\|fin\|rechazo` | Toca la melodía indicada |
| `/test?colores=1` | El LED recorre el indicador universal, de pH 0 a 14, en 15 s |
| `/test?rele=100` | Un pulso del relevador (50–200 ms) · ⚠️ enciende la bomba |
| `/test?cebar=300` | Llena la manguera (50–1000 ms) · ⚠️ enciende la bomba |
| `/diag` | Lee de vuelta los registros del LED (XN03) y del buzzer (XN44) |
| `/diag?rgb=R,G,B[,S]&modo=a\|b\|c` | Escribe en el LED con 3 formatos y lo lee de vuelta |
| `/diag?tono=HZ,MS&modo=a\|b` | Escribe un tono (MSB o LSB primero) y lo lee de vuelta |

### 5.11 Fiabilidad de la actualización por Wi-Fi
En modo AP + STA, el **ahorro de energía del Wi-Fi** ralentizaba la red propia: la subida tardaba unos 24 s y a veces se cortaba (*Empty reply*). Con `WiFi.setSleep(false)` y la **conexión al Wi-Fi del evento en pausa durante la OTA**, la subida baja a **12 s a la primera**.

---

## 6. Pruebas realizadas y resultados (19 nov 2026)

| Hora aprox. | Prueba | Resultado |
|---|---|---|
| 10:06 | Identificación del chip y **copia de seguridad** de toda la flash (16 MB) | ✅ ESP32-S3 rev 0.2 con 16 MB. Copia en `backup/` con SHA-256 |
| 10:10 | Escaneo I2C con el ESP32 suelto | 0 dispositivos, lo esperado sin la placa |
| 10:40 | Escaneo con el ESP32 en la placa (XN03 + XN44) | ✅ 0x03 y 0x2C responden; potenciómetro = 46 |
| 11:20 | Circuito completo | ❌ No respondía ningún módulo → se diagnosticó un módulo mal encajado que bloqueaba el bus |
| 11:45 | Tras volver a encajarlo | ✅ 0x03, 0x0D y 0x2C · **2704/2704 lecturas I2C correctas (100 %)** |
| 11:45 | Identidad de los módulos | ✅ XN03 modelo 3 FW 2.0.2 · XN44 modelo 44 FW 2.1.0 · ⚠️ XN13 devuelve 0xFF |
| 11:50 | Estabilidad de la sonda (18 lecturas) | ✅ ADC 607 ± 1 |
| 12:00 | Calibración | ✅ 607 = pH 6.95; lectura en la placa de 6.93–6.97 |
| 12:00 | OTA por HTTP | ✅ 7 s, con autenticación (401 sin clave) |
| 12:20 | Indicador universal | ✅ LED verde (`#0ACA00`) a pH 6.96 |
| 12:40 | Blynk | ✅ Dispositivo Online en 10 s; V0 = 6.95, V3 = REPOSO, V7 = verde |
| 13:30 | Dashboard propio + `/api/estado` | ✅ En la placa · JSON con datos en vivo |
| 13:32 | **Rechazo (requisito 3.1)** desde la API de Blynk (umbral 7, pH 6.92) | ✅ Rechazado: V2 volvió a 0, la bomba siguió apagada y V8 mostró "Rechazado: el pH ya está en o por debajo del umbral" |
| 13:40 | OTA tras la corrección | ✅ 12 s a la primera (antes ~24 s con cortes) |
| 13:40 | **Diagnóstico del buzzer** (escritura y lectura de vuelta) | ✅ TONE = `03 E8 03 E8` (1000 Hz, 1000 ms) · STAT 0 · ❓ el equipo no lo oyó |
| 13:40 | **Diagnóstico del LED RGB** | ✅ R/G/B guardados correctamente en 0x01–0x03 · ❓ el equipo no vio el recorrido de colores |
| 13:43 | Bomba | ⚠️ **El equipo informa que la bomba no se apaga**, mientras el firmware está en REPOSO con el relevador en apagado → sospecha de cableado en **NC en lugar de NA**, conexión directa, o relevador defectuoso (sus lecturas devuelven 0xFF) |
| 13:57 | **Identificación por UART** | ✅ XN03 = 2.0.2.R02 · XN44 = 2.1.0.R01 · **XN13 = 0.1** (firmware antiguo R1) |
| 14:00 | **Relevador por UART** (`XN13A+S=0` cada 2 s) | ✅ Responde `OK` en cada orden · versiones y confirmación visibles en el dashboard propio (panel "Módulos") |
| Pendiente | **Dosificación con agua real** | ⏳ |
| Pendiente | Medir la solución de pH 4 (segundo punto de calibración) | ⏳ |

---

## 7. Problemas encontrados y cómo se resolvieron
*Material útil para el pitch: muestra cómo depuró el equipo.*

| Problema | Diagnóstico | Solución |
|---|---|---|
| Ningún módulo respondía con el circuito completo | Las líneas SDA y SCL estaban en alto pero nadie respondía: un módulo mal encajado bloqueaba el bus | Volver a encajar los módulos alineando los pines. Después: 100 % de lecturas correctas |
| La portátil no detectaba el ESP32 | El kernel no registraba ningún evento USB: **el cable solo cargaba** | Usar un cable USB-C de datos |
| El cable USB choca con los módulos montados | Limitación física de la maqueta | **OTA por HTTP** y página local por Wi-Fi |
| El agua neutra se leía como pH 5.2 | La sonda tiene un desplazamiento de unas 96 unidades de ADC respecto a la tabla del fabricante | Calibración por software (607 = 6.95) y comandos `cal7` / `cal4` |
| **La bomba no se apagaba** y las lecturas del XN13 daban 0xFF | Por UART, el XN13 se identificó como **firmware 0.1** (R1, Songle), sin la tabla de registros I2C de la R2. La orden I2C de "apagar" (`01,00`) lo dejaba **encendido**, y se repetía cada 2 s | **Control del relevador por UART** (`XN13A+S=1/0`), con confirmación `OK`. Panel "Módulos" en el dashboard. También se corrigió `prueba_kit` |
| `espota` no funcionaba | El firewall de NixOS bloquea la conexión de vuelta del ESP32 | OTA por HTTP (`/update`), sin abrir puertos |
| Blynk aparecía "sin conexión" | El token estaba invalidado y la plantilla había cambiado | Se creó el dispositivo en la plantilla BomACER con un token nuevo y se amplió el tiempo de conexión a 3 s |
| No se podía vigilar la placa sin perder Internet | La red del evento aísla a los clientes entre sí | Lecturas puntuales por `ARCE-prueba` y supervisión continua por Blynk |

---

## 8. Valor añadido y propuestas

**Ya implementado:**
- **Operación sin nube:** página local y botón físico, con el control 100 % local.
- **Algoritmo adaptativo** con aprendizaje de la ganancia y tratamiento del retardo de mezcla.
- **Seguridad por capas:** 7 protecciones (sección 5.5).
- **OTA por Wi-Fi,** como en los equipos reales en campo.
- **Calibración en sitio,** guardada en memoria no volátil.

**Propuesto (orden sugerido):**
1. **Botón adicional del kit** como **paro de emergencia**: pulsación corta para iniciar o detener, pulsación larga para bloquear el sistema hasta rearmarlo. Se conecta a un GPIO libre (INT = 7 o PWM = 5) y a GND.
2. **Potenciómetro como panel local** para fijar el umbral sin celular.
3. **Demostración del filtro:** enviar la lectura cruda y la filtrada a Blynk, más una métrica de reducción del ruido.
4. **Métricas de precisión** al terminar: pH final, **error respecto al umbral**, mL, pulsos y tiempo.
5. **Estimación previa** de los mL y el tiempo necesarios, avisando si no alcanza la solución.
6. **Diagnóstico de la sonda:** detectar si está desconectada, congelada o saturada.
7. **Eventos de Blynk** para las notificaciones push.

**Impacto y escalabilidad:**
- **Agua de Puebla:** el mismo esquema de medir, pulsar, esperar y aprender sirve para la **cloración**. Controlar el pH es lo que hace eficaz al cloro.
- **Bajo costo:** la solución usa componentes estándar y módulos XIDE, y las plantillas de Blynk se pueden replicar en otros puntos de dosificación.

---

## 9. Guion sugerido para la presentación (≈ 5–7 min)

| # | Diapositiva | Contenido |
|---|---|---|
| 1 | Portada | Equipo ARCE, ITP, título "Control inteligente de pH IoT" |
| 2 | El problema | Dosificar a ojo provoca sobre o subdosis. El pH condiciona la eficacia del cloro (HOCl: 90 % a pH 6.5 frente a 10 % a pH 8.5) |
| 3 | Nuestra solución | Medir, decidir con un algoritmo adaptativo, actuar e informar. Funciona sin nube |
| 4 | Arquitectura | Diagrama de la sección 3: ESP32 → I2C → X-NODEs, Wi-Fi AP + STA, Blynk |
| 5 | Medición confiable | Filtro de 3 etapas, ±0.01 pH, calibración (el caso de los 96 ADC de desplazamiento) |
| 6 | El algoritmo | Restricciones (33 mL/s, 50 ms, 100 mL, mezcla lenta, sin retorno) → pulsa y espera → aprendizaje → por qué nunca se pasa |
| 7 | Seguridad | Las 7 protecciones |
| 8 | Experiencia de usuario | Indicador universal en el LED, sonidos, Blynk, página local, OTA |
| 9 | **Demo en vivo** | Umbral 6.5 → Dosificar → el LED pasa de verde a amarillo → se detiene → métrica del error |
| 10 | Resultados | Tabla de pruebas (sección 6) y precisión final |
| 11 | Retos que superamos | Tabla de la sección 7 |
| 12 | Impacto y siguientes pasos | Cloración en Agua de Puebla, escalabilidad, propuestas de la sección 8 |

---

## 10. Preguntas probables del jurado

- **¿Cómo filtran el ruido?** Con tres etapas: muestreo a 50 Hz, media recortada (descarta el 50 % extremo de 25 muestras) y suavizado EMA. Además, el control espera a que la lectura sea estable (< 0.01 pH/s) antes de decidir. En reposo, el resultado es ±0.01 pH.
- **¿Por qué pulsos y no dejar la bomba encendida hasta llegar?** A 33 mL/s, en un segundo entran 33 mL y la solución tarda en mezclarse. Con la bomba encendida hasta que la sonda marque el umbral, cuando la lectura reaccionara ya habríamos pasado el umbral, y no hay base para corregir.
- **¿Cómo evitan pasarse del umbral?** Cada pulso corrige solo el 60 % del error estimado. Además, si incluso el pulso mínimo de 50 ms nos llevaría por debajo del umbral, el sistema se detiene antes.
- **¿Qué pasa si se cae Internet?** El control es local. Se opera desde la página `ARCE-prueba` o con el botón, y Blynk solo supervisa.
- **¿Qué pasa si se acaba la solución o la manguera tiene aire?** Si 5 pulsos seguidos no cambian el pH, se detiene. También hay un límite de unos 82 mL por sesión.
- **¿Cómo calibraron?** Con un punto conocido (agua a pH 6.90–7.0 → ADC 607) y la pendiente del fabricante. El segundo punto (solución de pH 4) confirma la pendiente, con los comandos `cal7` y `cal4`.
- **¿Por qué el ESP32 no lee la sonda directamente?** Porque el XN03 hace la conversión A/D con su propio microcontrolador y aísla la parte analógica. Así el ESP32 solo usa I2C.
- **¿Cómo se relaciona con la cloración?** El cloro desinfecta como HOCl, y su proporción depende del pH. Controlar el pH es el primer paso para clorar bien, y el algoritmo se puede usar tal cual para dosificar hipoclorito.

---

## 11. Operación rápida

1. Encender la fuente de 12 V. Tras unos 10 s, el dispositivo aparece Online en Blynk.
2. Comprobar que el LED está **verde** con el agua a pH ~7.
3. Fijar el **umbral** con el slider de Blynk o en http://192.168.4.1.
4. Activar **Dosificar**. Suena la melodía de inicio y el sistema alterna pulsos y mezcla.
5. Al terminar suena la melodía de fin, el interruptor vuelve a 0 y el mensaje muestra el resultado.
6. **Emergencia:** *Detener* en la web o en Blynk, el botón BOOT, o **desconectar la fuente de 12 V**.

**Subir cambios:** `./subir_wifi.sh sketches/control_ph`, o http://192.168.4.1/update (usuario `arce`).

---

## 12. Pendientes

- [ ] **Prueba de dosificación con agua real**, con umbral 6.5 → anotar el pH final y el error.
- [ ] Medir la **solución de pH 4** para confirmar la pendiente (ADC ≈767 esperado).
- [x] **Bomba que no se apagaba:** causa = firmware 0.1 del XN13 + orden I2C incompatible → resuelto con control por UART.
- [ ] Reconectar la bomba en **COM + NA** y probar `/test?rele=100` (un pulso) con la manguera en líquido.
- [ ] Confirmar **físicamente** las melodías (subir a 2.5–4 kHz si no se oyen) y los colores del LED (subir el brillo).
- [ ] Confirmar que el **interruptor Dosificar** de Blynk arranca la dosificación con umbral < pH.
- [ ] Panel de **Blynk en la app** del celular.
- [ ] (Opcional) Eventos de Blynk, botón de emergencia, métricas de precisión.

---

## 13. Archivos del proyecto

| Archivo | Contenido |
|---|---|
| `DOCUMENTACION.md` | Este documento |
| `README.md` | Arranque rápido, estado y comandos |
| `PINES.md` | Registro verificado de pines, direcciones I2C, registros y lecturas |
| `XIDE_DISPOSITIVO.md` | Referencia del kit XIDE, el reto, erratas de la documentación oficial |
| `sketches/control_ph/` | ⭐ Firmware del reto (`secrets.h` excluido de git) |
| `prueba_kit/` | Firmware de diagnóstico (no enciende la bomba) con OTA |
| `sketches/escaner_i2c/` | Escáner del bus I2C |
| `subir_wifi.sh` | Subida de firmware por Wi-Fi |
| `backup/` | Copia del firmware original de la placa (excluida de git) |
| `.claude/skills/xide-hackathon/` | Skill para Claude Code con el contexto del kit |
