# Things that really ought to happen before this gets used on a dive
- Menu logic
- Poweroff/standby
- IWDG verify program state
- Error logging to eeprom emulation
- Startup sequencing issue:
ERR CODE 9(0x0) AT Core/Src/Hardware/ext_adc.c:105
ERR CODE 9(0x0) AT Core/Src/Hardware/ext_adc.c:119
ERR CODE 9(0x0) AT Core/Src/Hardware/ext_adc.c:119
ERR CODE 9(0x0) AT Core/Src/Hardware/ext_adc.c:119
ERR CODE 9(0x0) AT Core/Src/Hardware/ext_adc.c:119
ERR CODE 9(0x0) AT Core/Src/Hardware/ext_adc.c:119
ERR CODE 9(0x0) AT Core/Src/DiveCAN/PPO2Transmitter.c:87
ERR CODE 9(0x0) AT Core/Src/DiveCAN/PPO2Transmitter.c:87
ERR CODE 9(0x0) AT Core/Src/Sensors/DigitalOxygen.c:235
ERR CODE 9(0x0) AT Core/Src/DiveCAN/PPO2Transmitter.c:87
ERR CODE 3(0x0) AT Core/Src/Sensors/DigitalOxygen.c:123
ERR CODE 5(0x0) AT Core/Src/Sensors/DigitalOxygen.c:309

# Hardware changes
- Battery reading and power switching (REQUIRES HARDWARE CHANGE, BATTERY ADC unavail, should route to PC3)

# Things that ought to get done
- Menu for setting cell type
- Make DiveCAN multi-device safe so we can make multiple menu rows
- Predive "app", show loop pressure and such

# NASA rules of 10
    Avoid complex flow constructs, such as goto and recursion
    All loops must have fixed bounds (this prevents runaway code)
    Avoid heap memory allocation
    Restrict functions to a single printed page
    Use a minimum of two runtime assertions per function
    Restrict the scope of data to the smallest possible
    Check the return value of all nonvoid functions, or cast to void to indicate the return value is useless
    Use the preprocessor sparingly
    Limit pointer use to a single dereference, and do not use function pointers
    Compile with all possible warnings active; all warnings should then be addressed before the release of the software