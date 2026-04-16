#!/bin/bash
# OTA flash to the running ESP32-P4 cockpit device via HTTP.
# Uses TCP (reliable over slow WiFi) instead of ArduinoOTA's UDP.
#
# Usage:
#   ./ota.sh                       # uses 192.168.0.120
#   ./ota.sh 192.168.0.123         # explicit IP

set -e

TARGET=${1:-192.168.0.120}
PORT=${OTA_PORT:-8080}

echo "==> Building firmware..."
pio run

FIRMWARE=.pio/build/p4_cockpit/firmware.bin
if [ ! -f "$FIRMWARE" ]; then
    echo "Error: firmware.bin not found"
    exit 1
fi

SIZE=$(stat -c%s "$FIRMWARE")
echo "==> Uploading $FIRMWARE ($SIZE bytes) via HTTP to $TARGET:$PORT..."

curl --progress-bar \
    -X POST \
    --data-binary "@$FIRMWARE" \
    -H "Content-Type: application/octet-stream" \
    "http://$TARGET:$PORT/update"

echo ""
echo "==> Done — device will reboot"
