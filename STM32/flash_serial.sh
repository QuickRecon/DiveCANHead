
killall screen
stm32flash -vR -b 115200 -w build/STM32.hex /dev/ttyUSB0 && screen /dev/ttyUSB0 19200