# Flashing Firmware via SWD

## Overview

SWD (Serial Wire Debug) is a two-wire programming and debug interface standard for ARM microcontrollers. It allows you to flash firmware, set breakpoints, and inspect memory without using the bootloader or CAN bus.

The hardware chain for SWD flashing is: **PC** (running STM32CubeProgrammer) → **ST-Link V2** (USB debug probe) → **Adafruit Adapter** (20-pin to 10-pin converter) → **Target Board** (DiveCANHead).

## Hardware Requirements

| Item | Purpose | Approx Cost | Link |
|------|---------|-------------|------|
| ST-Link V2 | Debug probe connecting PC to target via USB | $3-25 | [Official](https://www.st.com/en/development-tools/st-link-v2.html), [Clone (Amazon)](https://www.amazon.com/s?k=st-link+v2) |
| Adafruit SWD to Cortex Debug Adapter | Converts ST-Link 20-pin header to ARM 10-pin SWD | ~$5 | [Adafruit](https://www.adafruit.com/product/2094) |
| 10-pin SWD ribbon cable | Connects adapter to target board | ~$2 | Often included with adapter |

**Note:** Clone ST-Link V2 devices work fine for flashing. The official version provides better reliability and faster speeds for debugging.

## Connecting the ST-Link to the Adapter

<!-- TODO: Add photos showing the ST-Link V2 connected to the Adafruit adapter -->
<!-- Photo should show: 20-pin header on ST-Link mating with the Adafruit adapter -->

The ST-Link V2 has a 20-pin IDC header. The Adafruit adapter plugs directly onto this header, converting it to the 10-pin Cortex Debug connector used by the target board.

1. Align the adapter with the ST-Link's 20-pin header
2. Ensure the keying notch matches (prevents incorrect insertion)
3. Press firmly until fully seated

## Connecting the Adapter to the Target Board

1. Connect the 10-pin ribbon cable to the Adafruit adapter
2. **Important:** The red stripe on the ribbon cable indicates pin 1 - ensure this aligns with pin 1 on both the adapter and target board
3. Connect the other end to the DiveCANHead's SWD header
4. **Power the target board before attempting to connect** - the ST-Link does not provide power to the target

## Software Installation

### Windows

1. Download STM32CubeProgrammer from [st.com](https://www.st.com/en/development-tools/stm32cubeprog.html)
2. Run the installer and accept the default options
3. The ST-Link USB driver is installed automatically

### macOS

1. Download STM32CubeProgrammer from [st.com](https://www.st.com/en/development-tools/stm32cubeprog.html)
2. Open the .dmg and drag STM32CubeProgrammer to Applications
3. If Gatekeeper blocks the app, go to System Preferences → Security & Privacy and click "Open Anyway"

### Linux

1. Download the .zip from [st.com](https://www.st.com/en/development-tools/stm32cubeprog.html)
2. Extract and run the installer:
   ```bash
   unzip en.stm32cubeprg-lin64_*.zip
   ./SetupSTM32CubeProgrammer-*.linux
   ```
3. **Install udev rules for USB permissions** (required):
   ```bash
   sudo cp /path/to/STM32CubeProgrammer/Drivers/rules/*.rules /etc/udev/rules.d/
   sudo udevadm control --reload-rules
   ```
4. Add your user to the dialout group:
   ```bash
   sudo usermod -aG dialout $USER
   ```
5. **Log out and back in** for group changes to take effect

## Flashing Procedure (GUI)

1. **Launch STM32CubeProgrammer**

2. **Select connection method**: In the right panel, ensure "ST-LINK" is selected (not UART or USB)

3. **Connect to target**: Click the green "Connect" button
   - You should see "Connected" status and target device info (STM32L4xxx)
   - The target's memory map will be displayed

4. **Open firmware file**: Click "Open file" and navigate to:
   ```
   STM32/build/STM32.elf
   ```
   (The .elf file contains both code and debug symbols)

5. **Flash the firmware**: Click "Download" to program the device
   - Progress bar shows flashing status
   - Wait for "File download complete" message

6. **Disconnect**: Click "Disconnect" to release the target

7. **Reset the board**: Power cycle or press the reset button to start the new firmware

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| ST-Link not detected | Driver or permissions issue | **Windows:** Reinstall STM32CubeProgrammer. **Linux:** Verify udev rules are installed and user is in dialout group |
| "No STM32 target found" | Bad connection or target unpowered | Check ribbon cable orientation (pin 1 alignment). Ensure target board has power |
| "Connection error" or timeout | Target in bad state | Hold reset button while clicking Connect, then release. Or power cycle the target |
| Permission denied (Linux) | User not in dialout group | Run `sudo usermod -aG dialout $USER`, then log out and back in |
| "Could not verify ST-Link" | Clone ST-Link firmware issue | Update ST-Link firmware via STM32CubeProgrammer's firmware upgrade option |
| Flashing succeeds but board doesn't run | Wrong start address or corrupt flash | Perform full chip erase before flashing, verify the .elf file is correct |

## Alternative: Command Line Flashing

STM32CubeProgrammer also provides a CLI tool (`STM32_Programmer_CLI`):

```bash
# Connect and flash
STM32_Programmer_CLI -c port=SWD -w STM32/build/STM32.elf -v -rst

# Options:
#   -c port=SWD    : Connect via SWD
#   -w <file>      : Write/flash the file
#   -v             : Verify after programming
#   -rst           : Reset target after flashing
```

## See Also

- [STM32CubeProgrammer User Manual](https://www.st.com/resource/en/user_manual/um2237-stm32cubeprogrammer-software-description-stmicroelectronics.pdf)
- `flash_serial.sh` - Alternative flashing via serial bootloader
- `flash_can.sh` - Alternative flashing via CAN bus
