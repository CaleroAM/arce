# Registro de pines y comunicaciones — Kit XIDE · Equipo ARCE

> Última verificación en hardware: **19 nov 2026, ~11:45**, con `prueba_kit` leído por Wi-Fi
> (ESP32 montado en la XBI01, alimentado solo por la fuente de 12 V → XN14).
>
> **Leyenda:** ✅ verificado en nuestra placa · 📄 según documentación oficial (sin verificar) · ⚠️ verificar antes de usar

---

## 1. Resumen rápido

```
           ESP32-S3 (XC01)                      Bus I2C compartido (todos los zócalos XBI01)
  ┌────────────────────────────┐        ┌───────────────────────────────────────────────┐
  │ GPIO12 ── SDA ─────────────┼────────┤ XN03 0x03  · XN13 0x0D  · XN44 0x2C           │
  │ GPIO13 ── SCL ─────────────┼────────┤                                               │
  │ GPIO16 ── LED de placa     │        └───────────────────────────────────────────────┘
  │ GPIO0  ── Botón BOOT       │
  └────────────────────────────┘
   Sonda pH ──(analógica)──► XN03 IN1 / IN2 ──(I2C)──► ESP32
   Bomba 12 V ◄──(contacto NA)── XN13 ◄──(I2C)── ESP32
```

**Regla clave:** el ESP32 **no lee la sonda ni mueve la bomba con sus propios GPIO**. Todo pasa por **I2C (GPIO12/13)** hacia los módulos X-NODE, que hacen la parte eléctrica.

```cpp
Wire.setPins(12, 13);   // SDA, SCL
Wire.begin();           // 100 kHz
```

---

## 2. Pines del ESP32-S3 (XC01)

| GPIO | Señal mikroBUS | Uso en el proyecto | Estado |
|---|---|---|---|
| **12** | **SDA** | Bus I2C de todos los módulos | ✅ |
| **13** | **SCL** | Bus I2C de todos los módulos | ✅ |
| **16** | LED de placa | Estado (parpadeos = módulos encontrados) | ✅ |
| **0** | Botón BOOT | Iniciar / detener dosificación · cambiar modo en `prueba_kit` | 📄 (pull-up) |
| 9 | RX (`Serial2`) | Libre (UART de los módulos, no se usa) | 📄 |
| 10 | TX (`Serial2`) | Libre | 📄 |
| 4 | AN | Libre · ADC1 (compatible con Wi-Fi) | 📄 |
| 5 | PWM | Libre | 📄 |
| 7 | INT | Libre | 📄 |
| 6 | CS | Libre | 📄 |
| 15 | RST | Libre | 📄 |
| 8 / 18 / 17 | SCK / MISO / MOSI | Libres (SPI) | 📄 |

> ⚠️ AN, RST, CS, PWM e INT **no llegan a los zócalos** de la XBI01 salvo que se unan con jumpers en el header de 40 pines.
> ⚠️ En el ESP32-S3, **ADC2 (GPIO 11–20) no funciona con Wi-Fi activo**: no usar esos pines para lecturas analógicas directas.

---

## 3. Módulos en el bus I2C

| Módulo | Dirección | Función | Estado |
|---|---|---|---|
| **XN03** Analog Inputs/Outputs | **`0x03`** | Lee la sonda (IN1/IN2), potenciómetro, LED RGB | ✅ responde · WHO_AM_I 3 rev 2 · FW 2.0.2 · STAT 0x00 |
| **XN13** Relay | **`0x0D`** | Enciende/apaga la bomba | ⚠️ contesta a su dirección, pero **todas sus lecturas devuelven 0xFF** (WHO_AM_I, FW, STAT y RELAY) |
| **XN44** Buzzer | **`0x2C`** | Alertas sonoras | ✅ responde · WHO_AM_I 44 rev 1 · FW 2.1.0 · STAT 0x00 |
| **XN14** Power Supply | — | Fuente 12 V → 3.3 V / 5 V (no tiene I2C) | ✅ alimenta la placa |

**Registros comunes a todos los X-NODE** (📄): `0x37` STAT · `0x38` FW (3 bytes) · `0x3E` WHO_AM_I (2 bytes).
> ⛔ **No escribir nunca** `0x3A` (TW_ADD) ni `0x39` (UART_ID): se guardan en memoria y cambian la dirección del módulo.

---

## 4. Registros que usa el firmware

### XN03 — `0x03`
| Registro | Qué es | Formato | Estado |
|---|---|---|---|
| **`0x21`** | **ADC IN1 = sonda de pH (Po)** | 2 bytes, MSB primero, 0–1023 (3.3 V) | ✅ lee **607 → 1.958 V** |
| `0x22` | ADC IN2 = temperatura de la sonda (To) | 2 bytes, 0–1023 | ✅ lee **383–386 (≈1.24 V)** · sin conversión a °C |
| **`0x10`** | Potenciómetro | 1 byte, 0–100 | ✅ lee **65** |
| `0x20` | ADC del potenciómetro | 2 bytes, 0–1023 | 📄 |
| `0x01` | LED RGB | escribir 3 bytes R, G, B (+ escala opcional) | ✅ funciona (morado confirmado) · color del pH según la tabla del reto |

**Calibración actual (19 nov):** ADC **607 = pH 6.95** (1 punto; pendiente de fábrica 54.33 ADC/pH)
`pH = 7 + (ADC − 604.3) × (−3) / (767.3 − 604.3)` → pH 7 ≈ 604.3 · pH 4 ≈ 767.3
Constantes: `CAL_ADC7 = 604.3`, `CAL_ADC4 = 767.3` (en `prueba_kit` y `control_ph`; `control_ph` las puede reajustar con `cal7`/`cal4`).

<small>Tabla original del fabricante, sin calibrar: pH 7 ≈ 511 · pH 4 ≈ 674.</small>

### XN13 — `0x0D`
| Registro | Qué es | Formato | Estado |
|---|---|---|---|
| **`0x01`** | **RELAY** | escribir `0x01` = encendido · `0x00` = apagado | 📄 · ⚠️ al leerlo **no devolvió `0x00`** tras escribir "apagado" |

> ⚠️ Antes de la primera prueba real del relevador: **desconectar la bomba** (o asegurar que está cebada con agua) y probar con pulsos de 50–100 ms. Una bomba de diafragma girando en seco se daña.

### XN44 — `0x2C`
| Registro | Qué es | Formato | Estado |
|---|---|---|---|
| **`0x01`** | **TONE** | 4 bytes: `[freqH, freqL, durH, durL]` · 31–20000 Hz · 1–60000 ms | 📄 · ¿sonaron los pitidos al encender? |

---

## 5. Cableado de la sonda de pH (diagrama oficial)

| Tarjeta de la sonda | → | XN03 |
|---|---|---|
| **Po** (salida de pH) | → | **IN1** |
| **To** (salida de temperatura) | → | **IN2** |
| Do | → | sin conectar |
| **V+** | → | **3V3** |
| **G** | → | **GND** |

> ⚠️ Las entradas del XN03 aceptan **máximo 3.3 V**. Alimentar la tarjeta de la sonda con **3V3**, no con 5 V.
> ⚠️ Si V+ y G quedan invertidos o algún cable toca SDA/SCL, **se bloquea todo el bus** y no responde ningún módulo (pasó durante las pruebas; se resolvió volviendo a encajar).

---

## 6. Cableado de la bomba (diagrama oficial)

```
 Fuente 12 V (+) ──────► XN13 COM
                         XN13 NA  ──────► Bomba (+)
 Fuente 12 V (−) ◄─────────────────────── Bomba (−)
                         XN13 NC  ──────  sin conectar
```

| Desde | → | Hacia |
|---|---|---|
| Fuente 12 V **(+)** | → | XN13 **COM** |
| XN13 **NA** (normalmente abierto) | → | Bomba **(+)** |
| Bomba **(−)** | → | Fuente 12 V **(−)** |

> ⚠️ Usar **NA**, nunca NC: con NC la bomba quedaría encendida siempre que el relevador esté apagado.
> ⚠️ Los 12 V de la bomba **no tocan** la lógica de la placa (solo la entrada de la XN14).

---

## 7. Alimentación

| Desde | → | Hacia |
|---|---|---|
| Fuente 12 V **(−)** | → | XN14 **GND(−)** |
| Fuente 12 V **(+)** | → | XN14 **12V(+)** |
| XN14 | → | XBI01 (3.3 V y 5 V para todos los zócalos) |

> ⚠️ XN14: alimentar **por jack o por clema, nunca los dos**. Fusible de 1 A.
> ✅ Verificado: con solo la fuente, el ESP32 arranca estable y los tres módulos responden.
> ⚠️ El cable USB-C actual **solo carga** (el portátil no lo detecta). Para subir código hace falta un cable **de datos**.

---

## 8. Lecturas de referencia (19 nov, ~11:45)

| Variable | Valor | Nota |
|---|---|---|
| ADC sonda (IN1) | 607 | estable en 3 lecturas |
| Voltaje sonda | 1.958 V | |
| pH (calibrado) | **6.95** | ✅ calibrado a 6.95 (rango real 6.90–7.0) · 18 lecturas: 607 estable, una en 608 (sonda viva) |
| Potenciómetro | 65 | |
| ADC IN2 (To) | 383–386 (≈1.24 V) | salida de temperatura, sin calibrar |
| Fiabilidad del bus I2C | **2704 / 2704 (100 %)** | cableado estable |
| Señal Wi-Fi del ESP32 | 82–92 % | red `ARCE-prueba` |

---

## 9. Pendientes de verificar

- [x] LED RGB del XN03: se ve morado con 3 bytes R,G,B en `0x01` (confirmado por el equipo) · ahora muestra el color del pH
- [ ] Buzzer XN44: ¿suenan los pitidos?
- [x] XN13: al leer cualquier registro devuelve **0xFF** (acepta su dirección y las escrituras, pero las lecturas no funcionan)
- [ ] XN13: probar lectura con *repeated start* (`endTransmission(false)`) y un pulso real con la bomba desconectada
- [ ] XN13: primer pulso real de 50–100 ms con la bomba cebada
- [x] Sonda: calibración de 1 punto (607 = pH 6.95)
- [ ] Sonda: confirmar la pendiente con un 2.º punto (solución pH 4 → ≈ 767 si el error es solo de cero; ≈ 800 si es de escala del ADC; ajustar con `cal4`)
- [x] IN2 (To): da ≈1.24 V estable (sin conversión a °C conocida)
- [x] WHO_AM_I / FW: XN03 = 3 / 2.0.2 · XN44 = 44 / 2.1.0 · XN13 = 0xFF (no legible)
