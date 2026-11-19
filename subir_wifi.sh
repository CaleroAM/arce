#!/usr/bin/env bash
# Sube un sketch al ESP32 por Wi-Fi (OTA HTTP), sin cable USB.
# Uso:  ./subir_wifi.sh [carpeta_del_sketch]      (por defecto: prueba_kit)
# Requisito: el sketch cargado en la placa debe tener la página /update (prueba_kit la tiene).
set -euo pipefail

SKETCH="${1:-prueba_kit}"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=disabled"
AP_SSID="ARCE-prueba"; AP_PASS="arce2026"; IP="192.168.4.1"
BUILD="$(mktemp -d)"
cd "$(dirname "$0")"

echo "== Compilando $SKETCH"
nix-shell -p arduino-cli --run "arduino-cli compile -b '$FQBN' --output-dir '$BUILD' '$SKETCH'" | grep -E 'error|El Sketch' || true
BIN=$(ls "$BUILD"/*.ino.bin)

PREV=$(nmcli -t -f NAME,TYPE connection show --active | grep wireless | cut -d: -f1 | head -1 || true)
echo "== Conectando a $AP_SSID (Wi-Fi actual: ${PREV:-ninguna})"
for i in 1 2 3 4 5; do                       # la red puede tardar en aparecer en el escaneo
  nmcli device wifi rescan >/dev/null 2>&1 || true; sleep 6
  nmcli -t -f SSID device wifi list | grep -qx "$AP_SSID" && break
  echo "   (buscando $AP_SSID, intento $i/5)"
done
nmcli device wifi connect "$AP_SSID" password "$AP_PASS" >/dev/null
sleep 3

echo "== Subiendo $(basename "$BIN") ($(stat -c%s "$BIN") bytes)"
curl -sS --max-time 120 -u "arce:$AP_PASS" -F "firmware=@$BIN" "http://$IP/update" || echo "ERROR en la subida"

echo "== Volviendo a ${PREV:-la red anterior}"
[ -n "$PREV" ] && nmcli connection up "$PREV" >/dev/null || true
nmcli connection delete "$AP_SSID" >/dev/null 2>&1 || true
rm -rf "$BUILD"
echo "== Listo"
