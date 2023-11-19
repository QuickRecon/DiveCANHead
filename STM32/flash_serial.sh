
killall screen
stm32flash -vR -w build/STM32.hex /dev/ttyUSB0 && screen /dev/ttyUSB0 9600