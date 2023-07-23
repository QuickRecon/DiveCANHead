pymcuprog erase -m flash -d avr64ea28 -t uart -u /dev/ttyUSB0 -v debug --packpath /home/aren/Archive/Dropbox/DiveCANHead/atpack/Atmel.AVR-Ex_DFP.2.5.176.atpack --uart-timeout 3 &&
pymcuprog read -m flash -d avr64ea28 -t uart -u /dev/ttyUSB0 -v debug -x > log.txt
pymcuprog read -m fuses -d avr64ea28 -t uart -u /dev/ttyUSB0 -v debug -x
pymcuprog erase -m flash -o 0x8000 -d avr64ea28 -t uart -u /dev/ttyUSB0 -v debug --packpath /home/aren/Archive/Dropbox/DiveCANHead/atpack/Atmel.AVR-Ex_DFP.2.5.176.atpack --uart-timeout 3
pymcuprog write -f main.bin -m flash -o 0x8000 -d avr64ea28 -t uart -u /dev/ttyUSB0 -v debug --packpath /home/aren/Archive/Dropbox/DiveCANHead/atpack/Atmel.AVR-Ex_DFP.2.5.176.atpack --uart-timeout 3 --verify
pymcuprog write -m flash -o 12090 -l 0x01 0x02 -d avr64ea28 -t uart -u /dev/ttyUSB0 -v debug --packpath /home/aren/Archive/Dropbox/DiveCANHead/atpack/Atmel.AVR-Ex_DFP.2.5.176.atpack --uart-timeout 3 --verify
pymcuprog read -m user_row -d avr64ea28 -t uart -u /dev/ttyUSB0 -v info 
