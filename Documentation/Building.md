# Core Firmware
Building depends on the arm-none-eabi-gcc compiler toolchain, as well as GNU make.

The code can be build by running make in the /STM32/ directory

# Unit Tests
Unit tests depend on cpputest, which is included in this repository as a module. Ensure it has been cloned and build it:
```
cd Stm32/Tests/cpputest
autoreconf -i
./configure
make
```

The unit tests can then be built and executed by running make in /STM32/Tests/

# Hardware Shim
It is an arduino project for the arduino due that is located under HWTesting/HardwareShim/HardwareShim.ino and is build via the arduino IDE
It depends on the Adafruit ADS_11X5 library
