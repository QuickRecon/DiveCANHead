#!/usr/bin/env bash
# Diagnostic: halt the chip and dump RTT control block + CPU state.
# Use to figure out where MCUBoot got stuck when no RTT output appears.

set -euo pipefail

NCS=/home/aren/ncs/toolchains/927563c840
export PATH=$NCS/usr/local/bin:$PATH
export LD_LIBRARY_PATH=$NCS/usr/local/lib:${LD_LIBRARY_PATH:-}

cd "$(dirname "$0")/.."

openocd \
    -f interface/stlink.cfg \
    -f target/stm32l4x.cfg \
    -c "init" \
    -c "halt" \
    -c "echo {--- PC / LR / xPSR ---}" \
    -c "reg pc" \
    -c "reg lr" \
    -c "reg xpsr" \
    -c "echo {--- SCB CFSR/HFSR/BFAR (fault status) ---}" \
    -c "mdw 0xe000ed28 1" \
    -c "mdw 0xe000ed2c 1" \
    -c "mdw 0xe000ed38 1" \
    -c "echo {--- _SEGGER_RTT control block @ 0x20000410 (first 96 B) ---}" \
    -c "mdb 0x20000410 96" \
    -c "echo {--- Up-buffer 0 ring: WrOff (off 0x18) and RdOff (off 0x1c) ---}" \
    -c "mdw 0x20000428 1" \
    -c "mdw 0x2000042c 1" \
    -c "echo {--- Slot0 byte at corruption offset 0x08011000 ---}" \
    -c "mdb 0x08011000 16" \
    -c "echo {--- MCUBoot vector table @ 0x08000000 ---}" \
    -c "mdw 0x08000000 4" \
    -c "shutdown"
