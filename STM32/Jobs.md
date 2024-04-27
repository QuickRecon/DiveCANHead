# Things that really ought to happen before this gets used on a dive
- Menu logic for displaying any error states, as well as firmware commit
- Work out why the brightness goes down when the CAN bus connects (protocol issue?)
- ADC intermittently doesn't come online after shutdown logic added, board reset required to fix.
- Battery voltage checks
- Update pwr management for new hardware

# Items to test
- Error logging to eeprom emulation

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