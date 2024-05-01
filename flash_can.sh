sudo ip link set can0 down
sudo ip link set can0 txqueuelen 100
sudo ip link set can0 type can bitrate 125000 sjw 2 #tdcv 0 tdco 0 tdcf 0 restart-ms 0
sudo ip link set can0 up
sleep 0.1

cansend can0 079# 
sleep 0.5
pushd /home/aren/Documents/Code/can-prog/canprog
export PYTHONPATH="/home/aren/Documents/Code/can-prog/:$PYTHONPATH"
/home/aren/Documents/Code/can-prog/.venv/bin/python /home/aren/Documents/Code/can-prog/canprog/main.py -n can0 stm32 write -evg /home/aren/Dropbox/DiveCANHeadRev2/STM32/build/STM32.hex 
popd
