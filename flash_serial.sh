#s#udo ip link set can0 down
#s#udo ip link set can0 txqueuelen 100
#sudo ip link set can0 type can bitrate 125000 sjw 2 tdcv 0 tdco 0 tdcf 0 restart-ms 0
#sudo ip link set can0 up
sleep 0.1

#cansend can0 079# 
sleep 0.1

killall screen
stm32flash -vR -b 115200 -w STM32/build/STM32.hex /dev/ttyUSB0 && screen /dev/ttyUSB0 19200
