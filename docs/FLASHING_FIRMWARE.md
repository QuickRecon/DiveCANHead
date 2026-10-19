# Flashing Zephyr firmware

Build with `--sysbuild` so the image set includes MCUboot and the signed
application. Follow the [root build guide](../README.md#build-firmware) and
select the actual board and fitted variant before flashing.

## Select the correct image

| Use | Local build output (from `Firmware/`) | Release package |
|---|---|---|
| Full SWD programming, Jr | `build/merged_divecan_jr_stm32l431xx.hex` | `*-full.hex` |
| Full SWD programming, MainPCB | `build-main/merged_divecan_main_stm32l431xx.hex` | Check board identity before using any package |
| UDS OTA application update | `<build-dir>/Firmware/zephyr/zephyr.signed.bin` | `*-ota.bin` |
| Application debugging | `<build-dir>/Firmware/zephyr/zephyr.elf` | Use matching local symbols |

The merged HEX carries MCUboot and the application at their intended addresses.
An application ELF alone does not install the bootloader. The old
`STM32/build/STM32.elf`, `flash_serial.sh`, and `flash_can.sh` instructions refer
to the removed legacy application.

The current internal layout reserves 36 KiB for MCUboot and 220 KiB for slot0.
Jr uses external SPI NOR; MainPCB uses a raw SD card for the secondary slot,
scratch, factory image, logs, and settings. MainPCB owns the first 64 MiB of
the card and does not mount a filesystem. See
[partition layout](../Firmware/ARCHITECTURE.md#flash-partitions).

## SWD programming

Connect a compatible ST-Link probe to the board's SWD signals, ground, target
voltage reference, and reset where available. Check the actual board/probe
pinout and power the target. Install STM32CubeProgrammer and the host's probe
permissions/driver support. The board definitions provide both OpenOCD and
STM32CubeProgrammer runners.

From `Firmware/`, after building the selected sysbuild directory:

```bash
# Jr build directory from the README:
west flash -d build --runner stm32cubeprogrammer

# MainPCB, if it was built into build-main:
west flash -d build-main --runner stm32cubeprogrammer
```

Run the command for the connected board. For manual STM32CubeProgrammer use,
open the corresponding merged HEX, download, verify, and reset. An equivalent
Jr CLI invocation is:

```bash
STM32_Programmer_CLI -c port=SWD -w build/merged_divecan_jr_stm32l431xx.hex -v -rst
```

## Jr helper and RTT

[`Firmware/flash.sh`](../Firmware/flash.sh) builds **Jr** into `Firmware/build/`,
programs via SWD, then starts an RTT console using OpenOCD:

```bash
cd Firmware
./flash.sh --variant Poseidon_Aren
```

The helper also attempts to set STM32 boot-from-flash and brown-out option
bytes (`nSWBOOT0=0`, `nBOOT0=1`, `BOR_LEV=1`). It is not a board-neutral wrapper.
Other modes are `--no-build`, `--rtt-only`, and `--erase`; `--erase` explicitly
mass-erases internal flash. When changing variants, first perform the pristine
build documented in the README, then use `--no-build` to flash that build.

Jr retains MCUboot RTT logging. MainPCB disables MCUboot text output to fit the
boot partition; application RTT remains available. Absence of a MainPCB boot
banner is therefore expected.

## OTA and releases

OTA transfers the signed application binary using the firmware's UDS update
protocol. Use an image for the intended board and variant and preserve the
MCUboot partition contract. See [UDS](../Firmware/UDS.md),
[the browser client](../DiveCAN_bt/README.md), and
[release packaging](../Firmware/docs/RELEASING.md).

## Troubleshooting

- **Probe missing or access denied:** check USB connectivity and the installed
  probe permissions/driver; serial-port group membership alone does not
  establish USB ST-Link access.
- **Target not found:** check board power, voltage reference, ground, SWD
  wiring, and reset. Under-reset connection can help if firmware enters a
  low-power state before the probe attaches.
- **Programming succeeds but startup fails:** verify board/variant identity,
  merged image selection, boot option bytes, and the required external storage.
  Internal SWD programming does not erase external settings or logs.
- **No boot text on MainPCB:** inspect application RTT and boot-history/image
  state; MCUboot logging is intentionally disabled there.
