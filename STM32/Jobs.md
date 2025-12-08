# Shitlist
- Power Reset during start with SD card
- O2S intermittent giving garbled data
- ADC intermittent timeout
- Add handler for new known messages (stop spurious logging)
- Solenoid will turn off during calibration if started during fire cycle
- Reset fatal error reason on clean shutdown
- Fix low voltage detection

# Watchlist
- O2S pipeline long latency (adjusted timings)
- Low battery error writing corrupting flash (added check)

# Items to test
- Error logging to eeprom emulation

# Things that ought to get done
- Menu for setting cell type
- Make DiveCAN multi-device safe so we can make multiple menu rows
- Predive "app", show loop pressure and such

# Hardware fixes
- external watchdog
- lower power can IC

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