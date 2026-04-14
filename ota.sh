#!/bin/bash
# OTA flash to the running ESP32-P4 cockpit device.
#
# Usage:
#   ./ota.sh                       # uses p4-cockpit.local via mDNS
#   ./ota.sh 192.168.0.123         # explicit IP
#   OTA_PASSWORD=xyz ./ota.sh ...  # custom password
#
# Requires: the device running firmware that called
#   enable_ota("cockpit-ota")
# which is the case for this project.

set -e

TARGET=${1:-p4-cockpit.local}
OTA_PASSWORD=${OTA_PASSWORD:-cockpit-ota}
ESPOTA=$(find ~/.platformio/packages/framework-arduinoespressif32 -name espota.py | head -1)

if [ -z "$ESPOTA" ]; then
    echo "Error: espota.py not found in PlatformIO packages"
    exit 1
fi

echo "==> Building firmware..."
pio run

FIRMWARE=.pio/build/p4_cockpit/firmware.bin
if [ ! -f "$FIRMWARE" ]; then
    echo "Error: firmware.bin not found at $FIRMWARE"
    exit 1
fi

SIZE=$(stat -c%s "$FIRMWARE")
echo "==> Flashing $FIRMWARE ($SIZE bytes) via OTA to $TARGET..."

python3 "$ESPOTA" \
    --ip="$TARGET" \
    --port=3232 \
    --auth="$OTA_PASSWORD" \
    --file="$FIRMWARE" \
    --progress

echo "==> Done"
