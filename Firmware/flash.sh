#!/bin/bash
# Flash DiveCAN Jr firmware via ST-Link (SWD) using west flash,
# then connect to the RTT console.
#
# Usage:
#   ./flash.sh                       # Build Poseidon_Aren and flash
#   ./flash.sh --variant AP_Aren     # Build another real variant and flash
#   ./flash.sh --no-build            # Flash only, skip build
#   ./flash.sh --rtt-only            # Skip build and flash, just connect RTT
#   ./flash.sh --erase               # Mass-erase the chip before flashing.
#                           # Needed when the chip has firmware that
#                           # enters STOP/SHUTDOWN before openocd can
#                           # halt it (e.g. the pre-MCUBoot image's
#                           # power_is_can_active() shutdown path).
#
# Requires: ST-Link connected, openocd installed.
# The ST-Link is also used for RTT console (Segger RTT over SWD),
# so no UART is consumed for debug output.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RTT_ONLY=false
NO_BUILD=false
ERASE=false
VARIANT="${DIVECAN_VARIANT:-Poseidon_Aren}"

while (($# > 0)); do
    case "$1" in
        --rtt-only) RTT_ONLY=true; NO_BUILD=true ;;
        --no-build) NO_BUILD=true ;;
        --erase)    ERASE=true ;;
        --variant)
            shift
            if (($# == 0)); then
                echo "ERROR: --variant requires a name" >&2
                exit 2
            fi
            VARIANT="$1"
            ;;
        --variant=*) VARIANT="${1#--variant=}" ;;
        *)
            echo "ERROR: unknown argument '$1'" >&2
            exit 2
            ;;
    esac
    shift
done

if [[ "$NO_BUILD" = false ]]; then
    if [[ ! -f "variants/$VARIANT.conf" ]] || \
       [[ ! -f "variants/$VARIANT.overlay" ]]; then
        echo "ERROR: variant '$VARIANT' needs both variants/$VARIANT.conf" \
             "and variants/$VARIANT.overlay" >&2
        exit 2
    fi
    if [[ -z "${ZEPHYR_SDK_INSTALL_DIR:-}" ]]; then
        echo "WARNING: ZEPHYR_SDK_INSTALL_DIR is unset; Zephyr will auto-detect" \
             "an SDK. See CLAUDE.md and verify it matches" \
             ".west-projects/zephyr/SDK_VERSION." >&2
    fi
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
    if [[ -f build/CMakeCache.txt ]] && \
       ! grep -q "^CMAKE_PROJECT_NAME:STATIC=sysbuild_toplevel" build/CMakeCache.txt; then
        echo "    build/ was non-sysbuild — clearing"
        rm -rf build
    fi
    # The .overlay sibling to .conf disables peripherals the variant
    # doesn't use, recovering ~1.8 KB RAM from driver state structs
    # that would otherwise be allocated for hardware never spoken to.
    # See the selected variants/<name>.overlay and reports/memory_analysis.md.
    ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
    west build -d build -b divecan_jr/stm32l431xx . --sysbuild \
        -- -DBOARD_ROOT=. \
           -DEXTRA_CONF_FILE="variants/$VARIANT.conf" \
           -DEXTRA_DTC_OVERLAY_FILE="variants/$VARIANT.overlay"
fi

if [[ "$RTT_ONLY" = false ]]; then
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

if [[ "$ERASE" = true ]]; then
    echo "=== Mass-erasing chip ==="
    # STM32CubeProgrammer's "under reset" (mode=UR) connect handles
    # chips that openocd can't halt — needed when firmware enters
    # STM32 SHUTDOWN mode within ~1 s of boot and the STLINK adapter
    # doesn't have NRST wired through. UR connect uses tight SWD
    # timing during the reset pulse to grab the chip before it
    # executes any user code.
    STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -e all
fi

if [[ "$RTT_ONLY" = false ]]; then
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

# RTT polling at 10 ms (default 100 ms). Drains the up-buffer ~10×
# faster so concurrent log bursts have headroom — relevant for the
# thread_analyzer paced dump (~20 messages over 1 s) and any panic /
# error storms. No throughput downside; openocd just polls more often.
openocd \
    -f interface/stlink.cfg \
    -f target/stm32l4x.cfg \
    -c "init" \
    -c "rtt setup 0x20000000 0x10000 \"SEGGER RTT\"" \
    -c "rtt polling_interval 10" \
    -c "rtt start" \
    -c "rtt server start 9090 0" \
    2>/dev/null &
OPENOCD_PID=$!

sleep 1

# socat exits on Ctrl-C, trap kills openocd
socat - TCP:localhost:9090

cleanup
