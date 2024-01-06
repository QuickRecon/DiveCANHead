cansend can0 005# 
#sleep 0.1
#cansend can0 079#00 
#sleep 0.1
#cansend can0 092#00
#sleep 0.1
#cansend can0 073#00
#sleep 0.1

pushd /home/aren/Documents/Code/can-prog/canprog
export PYTHONPATH="/home/aren/Documents/Code/can-prog/:$PYTHONPATH"
/home/aren/Documents/Code/can-prog/venv/bin/python /home/aren/Documents/Code/can-prog/canprog/main.py -n can0 stm32 erase
/home/aren/Documents/Code/can-prog/venv/bin/python /home/aren/Documents/Code/can-prog/canprog/main.py -n can0 stm32 write -g /home/aren/Dropbox/DiveCANHeadRev2/STM32/build/STM32.hex 
popd
