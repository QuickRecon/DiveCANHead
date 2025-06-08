#sudo slcand -o -c -s4 /dev/ttyCAN0 can0
sudo ip link set can0 down
sudo ip link set can0 txqueuelen 100
sudo ip link set can0 type can bitrate 125000 sjw 2 #tdcv 0 tdco 0 tdcf 0 restart-ms 0
sudo ip link set can0 up
sleep 0.1

cd /home/aren/DiveCANHeadRev2 ; /usr/bin/env /home/aren/DiveCANHeadRev2/.venv/bin/python /home/aren/DiveCANHeadRev2/Configurator/bootloader-test.py
popd
