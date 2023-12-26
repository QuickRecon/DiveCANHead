# Current State
Revision 2.1 hardware has been fabricated, programming is in its final stages with test dives expected soon.
Current software TODO is listed under STM32/Jobs.md

Constructive critisism, suggestions, and pull requests are always welcome.

# Goals
The goals of this project are pretty simple:
- Have a reliable PPO2 monitor that can handle both digital and analog PPO2 cell inputs
- Be extendable for future CCR technology developments
- Work with existing computers/handsets, accomplished via DiveCAN compatibility.

# The platform
The platform is based on an STM32 processor, connected to a pair of dual differential input ADCs for analog sensing. Digital cells are handled via the 3 UART ports on the STM32.
Power is done in a dual rail configuration, with a critical VCC rail running the main processor, CAN transiever, and other nessesary basic components. This is supplemented with a second VBUS rail used for running external peripherals like solid state sensors.
