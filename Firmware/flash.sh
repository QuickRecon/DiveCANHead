#!/bin/bash
# Flash DiveCAN Jr firmware via ST-Link (SWD) using west flash,
# then connect to the RTT console.
#
# Usage:
#   ./flash.sh              # Build (if needed) and flash
#   ./flash.sh --no-build   # Flash only, skip build
#   ./flash.sh --rtt-only   # Skip build and flash, just connect RTT
#
# Requires: ST-Link connected, openocd installed.
# The ST-Link is also used for RTT console (Segger RTT over SWD),
# so no UART is consumed for debug output.

set -e

NCS=/home/aren/ncs/toolchains/927563c840
export PATH=$NCS/usr/local/bin:$PATH
export LD_LIBRARY_PATH=$NCS/usr/local/lib:$LD_LIBRARY_PATH
export ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RTT_ONLY=false
NO_BUILD=false

for arg in "$@"; do
    case "$arg" in
        --rtt-only) RTT_ONLY=true; NO_BUILD=true ;;
        --no-build) NO_BUILD=true ;;
    esac
done

if [ "$NO_BUILD" = false ]; then
    echo "=== Building ==="
    west build -d build -b divecan_jr/stm32l431xx . \
        -- -DBOARD_ROOT=. -DEXTRA_CONF_FILE=variants/dev_full.conf
fi

if [ "$RTT_ONLY" = false ]; then
    echo "=== Flashing ==="
    west flash -d build
fi

echo "=== RTT Console (Ctrl-C to exit) ==="

cleanup() {
    kill $OPENOCD_PID 2>/dev/null
    wait $OPENOCD_PID 2>/dev/null
    exit 0
}
trap cleanup INT TERM

openocd \
    -f interface/stlink.cfg \
    -f target/stm32l4x.cfg \
    -c "init" \
    -c "rtt setup 0x20000000 0x10000 \"SEGGER RTT\"" \
    -c "rtt start" \
    -c "rtt server start 9090 0" \
    2>/dev/null &
OPENOCD_PID=$!

sleep 1

# socat exits on Ctrl-C, trap kills openocd
socat - TCP:localhost:9090

cleanup
