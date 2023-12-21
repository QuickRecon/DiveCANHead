# Things that really ought to happen before this gets used on a dive
- A proper error handling approach, using printfs is absolutely useless underwater
- Include timestamp on cell queue data
- Calibration routine
- Menu logic
- Poweroff/standby
- IWDG verify program state
- Two asserts per function
- Voting logic borked for D + no analog
- Debounce cal
- Work out why cal coeff calculation -> infinity



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