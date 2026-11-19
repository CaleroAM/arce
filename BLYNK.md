# Guía del dashboard de Blynk — Control de pH ARCE

Plantilla **BomACER** (`TMPL2XEggRshL`) · dispositivo **BomACER** · región NY3 · consola: https://blynk.cloud

> Dashboard de la **web de Blynk**. Los mismos datastreams sirven para la **app del celular** (ver §4).
> Otros dashboards: local con todos los controles en `ARCE-prueba` → http://192.168.4.1 · remoto público https://acer.tail2b1858.ts.net:8443 (controles con PIN).

---

## 1. Cada apartado del panel

| Apartado | Tipo de widget | Pin | ¿Se puede tocar? | Para qué sirve |
|---|---|---|---|---|
| **pH del agua** | Indicador (gauge) 0–14 | V0 | ❌ Solo muestra | pH medido por la sonda, ya **filtrado y calibrado**. **El arco cambia de color** según el indicador universal (verde ≈ 7, amarillo ≈ 6, naranja ≈ 4–5). Se actualiza cada 1 s |
| **Umbral de apagado** | Deslizador 4.5–7 (paso 0.1) | V1 | ✅ **Sí** | pH al que **la bomba se detiene sola**. Al soltar el deslizador el valor se envía al ESP32, que lo guarda aunque se reinicie |
| **Dosificar** | Interruptor | V2 | ✅ **Sí** | **Encendido** = empieza la **dosificación automática**. **Apagado** = la detiene. **Vuelve solo a apagado** cuando termina o cuando el ESP32 rechaza la orden (§3) |
| **Estado** | Etiqueta | V3 | ❌ | Qué hace el sistema: `REPOSO`, `DOSIFICANDO` (pulso de bomba en curso) o `MEZCLANDO` (esperando que la solución se mezcle antes de volver a medir) |
| **Bomba** | LED (círculo) | V5 | ❌ **No es un botón** | ⚪ vacío = bomba **apagada** · 🟢 relleno = bomba **encendida** (pulso). Se mantiene encendido 2 s tras cada pulso para que se vea |
| **Mensaje** | Etiqueta | V8 | ❌ | Último evento o resultado: inicio, rechazo, fin con el **error respecto al umbral** (§2) |
| **Solución dosificada (mL)** | Etiqueta | V6 | ❌ | mL de solución de pH 4 usados **en la dosificación actual** (33 mL/s × tiempo de bomba). Vuelve a 0 al iniciar otra |
| **pH y umbral en el tiempo** | Gráfica personalizada | V0 + V1 | Solo zoom y periodo | Historial del pH (línea) y del umbral. En la demo muestra cómo el pH baja por escalones, un escalón por pulso, hasta el umbral |

**Datastreams sin widget en el panel** (disponibles para la app o para ampliar el panel):

| Pin | Datastream | Qué contiene |
|---|---|---|
| V4 | Voltaje | Voltaje de la sonda (0–3.3 V), útil para calibrar |
| V7 | Color | Color del indicador en `#RRGGBB` |
| V9 | Temp_V | Salida de temperatura de la sonda (voltaje, sin convertir a °C) |

---

## 2. Mensajes posibles (apartado "Mensaje", V8)

| Mensaje | Qué significa |
|---|---|
| `Listo` | Recién encendido, en reposo |
| `Dosificacion iniciada: pH 6.90 -> 6.50` | Empezó a dosificar desde el pH actual hacia el umbral |
| `Rechazado: el pH ya esta en o por debajo del umbral` | Se pulsó **Dosificar** con pH ≤ umbral + 0.05 → **no enciende la bomba** (requisito 3.1). Baja el umbral si quieres dosificar |
| `Rechazado: lectura de pH invalida` | La sonda no da lectura válida → revisar la conexión de la sonda |
| `Umbral alcanzado. pH=6.52 umbral=6.50 error=+0.02 pulsos=4 dosis=6.6 mL` | ✅ **Terminó bien.** `error` = pH final − umbral (cuanto más cerca de 0, mejor) |
| `Lo mas cerca posible sin pasarse. …` | ✅ Terminó: el siguiente pulso, incluso el mínimo, lo habría dejado por debajo del umbral |
| `Limite de solucion alcanzado. …` | Se usaron ≈82 mL (de 100). Rellenar o cambiar el agua |
| `La bomba no produce cambio (revisar manguera/solucion). …` | 5 pulsos sin que bajara el pH: manguera sin cebar, contenedor vacío o manguera fuera del líquido |
| `Sensor de pH sin lectura. …` | Se perdió la lectura de la sonda durante la dosificación → se detiene |
| `Cancelado por el usuario. …` | Se apagó **Dosificar** a mitad de la dosificación |
| `Actualizacion de firmware. …` | Se detuvo para actualizar el programa por Wi-Fi |

---

## 3. Cómo se usa (flujo típico)

1. Comprobar que el dispositivo sale **Online** (punto verde junto a "BomACER").
2. Mirar **pH del agua**: con el agua del reto, ≈ 6.9–7.0 (verde).
3. Mover **Umbral de apagado** al valor deseado (para la demo, **6.5**), y soltarlo.
4. Encender **Dosificar**:
   - **Mensaje** → `Dosificacion iniciada…`, y suena la melodía **ascendente** en la maqueta.
   - **Estado** alterna `DOSIFICANDO` (0.1–0.6 s, LED **Bomba** 🟢) y `MEZCLANDO` (6–30 s).
   - **Solución dosificada** sube y **pH del agua** baja (verde → amarillo).
5. Al llegar al umbral: **Dosificar** vuelve **solo** a apagado, **Mensaje** muestra el resultado con el **error**, y suena la melodía **descendente**.
6. **Para detener a mitad:** apagar **Dosificar**.

> Si al encender **Dosificar** vuelve a apagado enseguida → leer **Mensaje**: casi siempre es que el pH ya está por debajo del umbral.

---

## 4. App del celular (mismos datastreams)

| Widget de la app | Datastream | Ajustes recomendados |
|---|---|---|
| **Gauge** "pH del agua" | V0 | 0–14. El color lo cambia el firmware |
| **Slider** "Umbral" | V1 | 4.5–7, paso 0.1, *Send on release* |
| **Button** "Dosificar" | V2 | Modo **Switch** (no Push), 0/1 |
| **Value Display** "Estado" | V3 | — |
| **LED** "Bomba" | V5 | — |
| **Value Display** "Mensaje" | V8 | Fuente pequeña (texto largo) |
| **Value Display** "Solución (mL)" | V6 | 1 decimal |
| **SuperChart** | V0 (+ V1) | Periodo "Live" o "1h" para la demo |

---

## 5. Problemas frecuentes

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| Todo en `--` / dispositivo **Offline** | ESP32 apagado, o sin Wi-Fi `hackathon2026` | Encender la fuente de 12 V; esperar ~10 s |
| **Dosificar** se apaga solo al pulsarlo | Orden **rechazada** (pH ≤ umbral) | Leer **Mensaje**; bajar el umbral |
| El LED **Bomba** nunca se enciende durante la dosificación | Los pulsos aún no han empezado, o la orden se rechazó | Mirar **Estado** y **Mensaje** |
| La bomba se mueve con **Dosificar** apagado | Cableado en **NC** en vez de **NA**, o conexión directa | Apagar la fuente; conectar **COM + NA** en el XN13 |
| El pH no cambia aunque la bomba funciona | Manguera fuera del líquido o sin cebar | Revisar las mangueras; el sistema se detiene solo tras 5 pulsos sin efecto |
| El pH tarda mucho en estabilizarse | La solución tarda en mezclarse | Normal: el sistema espera hasta 30 s por pulso. Agitar suavemente ayuda |
