# simple AVR Makefile
#
# written by michael cousins (http://github.com/mcous)
# released to the public domain

# Makefile
#
# targets:
#   all:    compiles the source code
#   test:   tests the isp connection to the mcu
#   flash:  writes compiled hex file to the mcu's flash memory
#   fuse:   writes the fuse bytes to the MCU
#   disasm: disassembles the code for debugging
#   clean:  removes all .hex, .elf, and .o files in the source code and library directories

# parameters (change this stuff accordingly)
# project name
PRJ = main
# avr mcu
MCU = avr64ea28
# mcu clock frequency
CLK = 10000000
# avr programmer (and port if necessary)
# e.g. PRG = usbtiny -or- PRG = arduino -P /dev/tty.usbmodem411
#PRG = usbtiny
# fuse values for avr: low, high, and extended
# these values are from an Arduino Uno (ATMega328P)
# see http://www.engbedded.com/fusecalc/ for other MCUs and options
#LFU = 0xFF
#HFU = 0xDE
#EFU = 0x05
# program source files (not including external libraries)
SRC = main.cpp DiveCAN/DiveCANDevice.cpp DiveCAN/CellState.cpp OxygenSensing/AnalogCell.cpp OxygenSensing/DigitalCell.cpp
# where to look for external libraries (consisting of .c/.cpp files and .h files)
# e.g. EXT = ../../EyeToSee ../../YouSART
EXT = mcc_generated_files/system/src mcc_generated_files/timer/src mcc_generated_files/spi/src mcc_generated_files/reset/src mcc_generated_files/nvm/src mcc_generated_files/i2c_host/src mcc_generated_files/adc/src mcc_generated_files/uart/src mcc_generated_files/vref/src CAN_lib/


#################################################################################################
# \/ stuff nobody needs to worry about until such time that worrying about it is appropriate \/ #
#################################################################################################

# include path
INCLUDE := $(foreach dir, $(EXT), -I$(dir))
# c flags
CFLAGS    = -Wall -Os -DF_CPU=$(CLK) -mmcu=$(MCU) -B /home/aren/Archive/Dropbox/DiveCANHead/atpack/gcc/dev/avr64ea28 $(INCLUDE)
# any aditional flags for c++
CPPFLAGS =

# executables
AVRDUDE = avrdude -c $(PRG) -p $(MCU)
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE    = avr-size --format=avr --mcu=$(MCU)
CC      = avr-gcc
CPP     = avr-g++

# generate list of objects
CFILES    = $(filter %.c, $(SRC))
ASM    = $(foreach dir, $(EXT), $(wildcard $(dir)/*.S))
EXTC     := $(foreach dir, $(EXT), $(wildcard $(dir)/*.c))
CPPFILES  = $(filter %.cpp, $(SRC))
EXTCPP   := $(foreach dir, $(EXT), $(wildcard $(dir)/*.cpp))
OBJ       = $(CFILES:.c=.o) $(EXTC:.c=.o) $(CPPFILES:.cpp=.o) $(EXTCPP:.cpp=.o) $(ASM:.S=.o)

# user targets
# compile all files
all: $(PRJ).hex

## write fuses to mcu
#fuse:
#	$(AVRDUDE) -U lfuse:w:$(LFU):m -U hfuse:w:$(HFU):m -U efuse:w:$(EFU):m

# generate disassembly files for debugging
disasm: $(PRJ).elf
	$(OBJDUMP) -d $(PRJ).elf

# remove compiled files
clean:
	rm -f *.hex *.elf *.o
	$(foreach dir, $(EXT), rm -f $(dir)/*.o;)

flash:
# pymcuprog write -f $(PRJ).hex -d avr64ea28 -t uart -u /dev/ttyUSB0 --packpath /home/aren/Archive/Dropbox/DiveCANHead/atpack/Atmel.AVR-Ex_DFP.2.5.176.atpack --uart-timeout 3 --verify --erase
	avrdude -p avr64ea28 -c SerialUPDI -P /dev/ttyUSB0 -U flash:w:main.hex:i

# other targets
# objects from c files
.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

.S.o:
	$(CC) $(CFLAGS) -c $< -o $@

# objects from c++ files
.cpp.o:
	$(CPP) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# elf file
$(PRJ).elf: $(OBJ)
	$(CPP) $(CFLAGS) -Wl,--section-start=.text=0x8000 -o $(PRJ).elf $(OBJ)

# hex file
$(PRJ).hex: $(PRJ).elf
	rm -f $(PRJ).hex $(PRJ).hex
	$(OBJCOPY) -O ihex $(PRJ).elf  $(PRJ).hex
	$(SIZE) --format=avr --mcu=avr64ea28 $(PRJ).elf
