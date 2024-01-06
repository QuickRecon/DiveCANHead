# Things that really ought to happen before this gets used on a dive
- Menu logic, this should allow for cell mode switching, display any error states, as well as firmware commit
- IWDG verify program state
- Error logging to eeprom emulation

# Hardware changes
- Work out why standby power consumption is 10 mA (this was the CAN PWR Boost-Buck, root cause TBD)
- Battery reading and power switching (REQUIRES HARDWARE CHANGE, BATTERY ADC unavail, should route to PC3)
- CAN_EN should be routed to any of PA0, PC13, PE6, PA2, or PC5 so we can go to standby/shutdown rather than just STOP
- Expand CAN voltage tolerance, 9v batter suitable (12v so we have headroom?)
- DAC output to cell port
- Add pulldowns on some pins (HW version identification, so we can do common core software)

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