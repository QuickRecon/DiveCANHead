sudo ip link set can0 down
sudo ip link set can0 txqueuelen 100
sudo ip link set can0 type can bitrate 125000 sjw 2 #tdcv 0 tdco 0 tdcf 0 restart-ms 0
sudo ip link set can0 up