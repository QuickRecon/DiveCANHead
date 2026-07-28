#!/usr/bin/env bash
# Negative-path test for MCUBoot SHA-256 validation (Phase 2).
#
# Procedure:
#   1. Flip one byte deep inside the body of zephyr.signed.bin.
#   2. Reflash ONLY slot0 (0x08010000) with the corrupted image —
#      MCUBoot itself stays untouched.
#   3. Halt the chip via openocd, then:
#        a. Read PC and verify it sits in MCUBoot text (< 0x08010000).
#        b. Read the SEGGER RTT up-buffer descriptor and dump the
#           buffer contents — that's MCUBoot's complete boot log,
#           captured live by MCUBoot before it entered FIH_PANIC.
#   4. Print the log + a PASS/FAIL verdict.
#
# Why we read the RTT buffer directly instead of streaming over a
# socket: MCUBoot writes its log lines in ~10–50 ms during early boot,
# then sits forever in FIH_PANIC. By the time openocd attaches and the
# `rtt server` polling loop notices the control block, the writes are
# long over — the existing buffer content is the only evidence. A
# memory dump from `0x20000010 .. +WrOff` is deterministic and avoids
# the host-attach race entirely.
#
# Recovery: re-run ./flash.sh to restore a valid slot0.

set -euo pipefail

NCS=/home/aren/ncs/toolchains/927563c840
export PATH=$NCS/usr/local/bin:$PATH
export LD_LIBRARY_PATH=$NCS/usr/local/lib:${LD_LIBRARY_PATH:-}
export ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/.."

SIGNED_BIN="build/Firmware/zephyr/zephyr.signed.bin"
CORRUPT_BIN="build/Firmware/zephyr/zephyr.signed.corrupt.bin"
SLOT0_ADDR="0x08010000"
MCUBOOT_END=0x08010000
FLIP_OFFSET=4096          # past the 512 B header, before the TLV

# RTT control block layout (matches CONFIG_SEGGER_RTT_SECTION_CUSTOM
# placement at __rtt_buff_data_start = 0x20000000 + offsets the linker
# script picks). _SEGGER_RTT lives at 0x20000410 on both MCUBoot and
# app builds (verified via nm). aUp[0] starts 24 bytes in.
RTT_CB=0x20000410
RTT_PBUFFER_ADDR=0x2000042c   # &aUp[0].pBuffer
RTT_WROFF_ADDR=0x20000434     # &aUp[0].WrOff

if [[ ! -f "$SIGNED_BIN" ]]; then
    echo "ERROR: $SIGNED_BIN not found — run the build first." >&2
    exit 1
fi

cp "$SIGNED_BIN" "$CORRUPT_BIN"

python3 - "$CORRUPT_BIN" "$FLIP_OFFSET" <<'PY'
import sys
path, offset = sys.argv[1], int(sys.argv[2])
with open(path, 'r+b') as f:
    f.seek(offset)
    orig = f.read(1)
    flipped = bytes([orig[0] ^ 0xFF])
    f.seek(offset)
    f.write(flipped)
    print(f"flipped byte at 0x{offset:x}: 0x{orig.hex()} -> 0x{flipped.hex()}")
PY

echo
echo "=== Flashing corrupted slot0 (MCUBoot untouched) ==="
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst \
    -d "$CORRUPT_BIN" "$SLOT0_ADDR" -v

echo
echo "=== Halting chip to inspect rejection ==="
sleep 1

OCD_OUT=$(mktemp)
openocd \
    -f interface/stlink.cfg \
    -f target/stm32l4x.cfg \
    -c "init" -c "halt" \
    -c "echo {---PC---}" \
    -c "reg pc" \
    -c "echo {---RTT_MAGIC---}" \
    -c "mdb $RTT_CB 10" \
    -c "echo {---RTT_PBUFFER---}" \
    -c "mdw $RTT_PBUFFER_ADDR 1" \
    -c "echo {---RTT_WROFF---}" \
    -c "mdw $RTT_WROFF_ADDR 1" \
    -c "shutdown" >"$OCD_OUT" 2>&1 || true

PC=$(awk '/---PC---/{flag=1;next} flag && /pc/{print $NF; exit}' "$OCD_OUT")
RTT_MAGIC=$(awk '/---RTT_MAGIC---/{flag=1;next} flag && /^0x/{print; exit}' "$OCD_OUT")
PBUFFER=$(awk '/---RTT_PBUFFER---/{flag=1;next} flag && /^0x/{print $2; exit}' "$OCD_OUT")
WROFF=$(awk '/---RTT_WROFF---/{flag=1;next} flag && /^0x/{print $2; exit}' "$OCD_OUT")

echo
echo "PC at halt : $PC"

# Decode RTT magic. ASCII "SEGGER RTT" = 53 45 47 47 45 52 20 52 54 54.
# When MCUBoot panics very early (before the logging subsystem inits),
# the magic is never written and the pBuffer/WrOff values would be
# garbage — skip the dump in that case.
if [[ "$RTT_MAGIC" == *"53 45 47 47 45 52 20 52 54 54"* ]]; then
    BYTES=$((16#$WROFF))
    if (( BYTES > 0 && BYTES <= 1024 )); then
        echo
        echo "=== MCUBoot RTT log (pBuffer=0x$PBUFFER WrOff=$BYTES B) ==="
        BUF_OUT=$(mktemp)
        openocd \
            -f interface/stlink.cfg \
            -f target/stm32l4x.cfg \
            -c "init" -c "halt" \
            -c "mdb 0x$PBUFFER $BYTES" \
            -c "shutdown" >"$BUF_OUT" 2>&1 || true
        grep "^0x2000" "$BUF_OUT" | \
            awk '{for(i=2;i<=NF;i++) printf "%s ", $i; print ""}' | \
            xxd -r -p
        rm -f "$BUF_OUT"
    fi
else
    echo "RTT log    : not captured (MCUBoot panicked before RTT init)"
fi

# PC verdict.
PASS=false
if [[ -n "$PC" ]]; then
    PC_DEC=$((PC))
    if (( PC_DEC >= 0x08000000 )) && (( PC_DEC < MCUBOOT_END )); then
        PASS=true
    fi
fi

echo
if $PASS; then
    echo "=== PASS: MCUBoot rejected the corrupt image ==="
    echo "PC sits inside MCUBoot text (< 0x08010000) — the corrupted"
    echo "slot0 was not executed. Run ./flash.sh to restore."
    rm -f "$OCD_OUT"
    exit 0
else
    echo "=== FAIL: PC outside expected MCUBoot range ==="
    echo "Either MCUBoot jumped to the corrupt slot0, or openocd could"
    echo "not read the chip state. Full openocd output:"
    echo
    cat "$OCD_OUT"
    rm -f "$OCD_OUT"
    exit 1
fi
