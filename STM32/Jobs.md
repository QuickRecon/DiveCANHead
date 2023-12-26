# Things that really ought to happen before this gets used on a dive
- Menu logic
- Poweroff/standby
- IWDG verify program state
- Two asserts per function
- Handle cal failure
- Add -Wextra -Wpedantic -Wconversion to the cflags
- Battery reading and power switching
- Startup sequencing issue

ERR CODE 5(0x0) AT Core/Src/Hardware/ext_adc.c:62
ERR CODE 5(0x0) AT Core/Src/Hardware/ext_adc.c:74
ERR CODE 5(0x0) AT Core/Src/Hardware/ext_adc.c:74
ERR CODE 5(0x0) AT Core/Src/Hardware/ext_adc.c:74
ERR CODE 5(0x0) AT Core/Src/Hardware/ext_adc.c:74
ERR CODE 5(0x0) AT Core/Src/Hardware/ext_adc.c:74
ERR CODE 5(0x0) AT Core/Src/DiveCAN/PPO2Transmitter.c:76
ERR CODE 5(0x0) AT Core/Src/DiveCAN/PPO2Transmitter.c:76
ERR CODE 5(0x0) AT Core/Src/DiveCAN/PPO2Transmitter.c:76


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