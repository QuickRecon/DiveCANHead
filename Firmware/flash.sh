#!/bin/bash
# Flash DiveCAN Jr firmware via ST-Link (SWD) using west flash,
# then connect to the RTT console.
#
# Usage:
#   ./flash.sh              # Build (if needed) and flash
#   ./flash.sh --no-build   # Flash only, skip build
#   ./flash.sh --rtt-only   # Skip build and flash, just connect RTT
#   ./flash.sh --erase      # Mass-erase the chip before flashing.
#                           # Needed when the chip has firmware that
#                           # enters STOP/SHUTDOWN before openocd can
#                           # halt it (e.g. the pre-MCUBoot image's
#                           # power_is_can_active() shutdown path).
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
ERASE=false

for arg in "$@"; do
    case "$arg" in
        --rtt-only) RTT_ONLY=true; NO_BUILD=true ;;
        --no-build) NO_BUILD=true ;;
        --erase)    ERASE=true ;;
    esac
done

if [ "$NO_BUILD" = false ]; then
    echo "=== Building ==="
    # --sysbuild pulls in MCUBoot as a child image. The resulting
    # build/merged_<board>.hex contains bootloader + signed app and
    # is what `west flash` programs.
    #
    # If an existing build/ was configured without --sysbuild, the
    # CMake cache mismatches the sysbuild source root and west aborts.
    # Detect that case and wipe build/ before retrying. The sysbuild
    # top-level cache sets CMAKE_PROJECT_NAME to "sysbuild_toplevel"
    # — its absence means this is a plain (non-sysbuild) build dir.
    if [ -f build/CMakeCache.txt ] && \
       ! grep -q "^CMAKE_PROJECT_NAME:STATIC=sysbuild_toplevel" build/CMakeCache.txt; then
        echo "    build/ was non-sysbuild — clearing"
        rm -rf build
    fi
    west build -d build -b divecan_jr/stm32l431xx . --sysbuild \
        -- -DBOARD_ROOT=. -DEXTRA_CONF_FILE=variants/dev_full.conf
fi

if [ "$RTT_ONLY" = false ]; then
    # Force option bytes to ignore the physical BOOT0 pin and always boot
    # from main flash. The Jr boards we've seen have BOOT0 floating high,
    # which sends every reset to the STM32 ROM bootloader and breaks
    # MCUBoot. nSWBOOT0=0 takes the boot decision from nBOOT0 (=1, main
    # flash) only. This is idempotent — re-running on an already-set
    # chip is a no-op. See COMPROMISE.md #9 for background.
    echo "=== Ensuring boot-from-flash option bytes ==="
    STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst \
                         -ob nSWBOOT0=0 nBOOT0=1 2>&1 \
                         | tail -3 || true
fi

if [ "$ERASE" = true ]; then
    echo "=== Mass-erasing chip ==="
    # STM32CubeProgrammer's "under reset" (mode=UR) connect handles
    # chips that openocd can't halt — needed when firmware enters
    # STM32 SHUTDOWN mode within ~1 s of boot and the STLINK adapter
    # doesn't have NRST wired through. UR connect uses tight SWD
    # timing during the reset pulse to grab the chip before it
    # executes any user code.
    STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -e all
fi

if [ "$RTT_ONLY" = false ]; then
    echo "=== Flashing ==="
    # Same rationale as the erase path: use the stm32cubeprogrammer
    # runner instead of openocd. The runner is pre-configured in the
    # board's runners.yaml so this is just a one-flag swap.
    west flash -d build --runner stm32cubeprogrammer
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
