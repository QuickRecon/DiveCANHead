# Serial Port and CAN (recommended)
Connecting a USB TTY adaptor to RX2 and TX2 as marked on the board, one can use the STM32Flash utility to upload the firmware onto the board. If a USB CANBus adaptor is also connected as can0, the script `flash_serial.sh` will automatically put the board into bootloader via the CANBus the upload via the serial port (assumed to be ttyUSB0). This is the most reliable way of flashing and requires no physical interaction with the board can be flashed using the serial port alone. However, this will require using the physical boot and reset buttons on the board to enter the bootloader, hold down the boot button and press reset to enter the bootloader.

When in the bootloader, only LED 7 is illuminated, when running the flashed firmware LED0 is illuminated.

# CAN only
Using the `flash_can.sh` script it is possible to upload firmware to the board just using the CANBus, however it depends on a custom fork of the can-prog (https://github.com/QuickRecon/can-prog) which has not proven itself to be entirely reliable. It is common for the flashing to fail when verifying the contents of flash, or for there to be a presumed overflow of the CANBus TX queue which can leave the bootloader unresponsive until manually reset.
