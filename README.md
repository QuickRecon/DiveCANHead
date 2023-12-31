# Current State
Revision 2.1 hardware has been fabricated, programming is in its final stages with test dives expected soon.
Current software TODO is listed under STM32/Jobs.md

Constructive criticism, suggestions, and pull requests are always welcome.

# Goals
The goals of this project are pretty simple:
- Have a reliable PPO2 monitor that can handle both digital and analog PPO2 cell inputs
- Be extendable for future CCR technology developments
- Work with existing computers/handsets, accomplished via DiveCAN compatibility.

# DiveCAN
DiveCAN is Shearwaters proprietary digital rebreather communications bus, more details about the electrical and protocol layers are discussed here: https://github.com/QuickRecon/DiveCAN


# The platform
The platform is based on an STM32 processor, connected to a pair of dual differential input ADCs for analog sensing. Digital cells are handled via the 3 UART ports on the STM32.
Power is done in a dual rail configuration, with a critical VCC rail running the main processor, CAN transceiver, and other necessary basic components. This is supplemented with a second VBUS rail used for running external peripherals like solid state sensors.

The board has a battery onboard which can provide full functionality, and can also be provided with power from the CAN bus (1.8-5.5v). The VCC rail will prioritize CAN power and revert to battery if that is unavailable, the VBUS rail can have its power source controlled via the firmware. The VBUS rail can also be fed onto the CAN bus, allowing the board to act as a CAN power source, supplementing the bus power of the shearwater (which tops out at 16 mA).

Fundamentally the hardware is an IO board, bridging generic cells, sensors, and other peripherals to the DiveCAN bus. To do this it provides the following:
- 3 uart ports
- 4 analog differential inputs (3 terminated for O2 cells, 1 unterminated differential input)
- the solenoid output (5.5v, max 800mA)
- general DC out (3.3v, 800mA)
- the CAN bus itself

Currently there are sensor drivers for analog oxygen cells and digital oxygen cells. Future development.

The board is currently emulating a DiveCAN SOLO board, specifically using data captured from a JJ head, and testing with a JJ Petrel 3. This will almost certainly work with any DiveCAN shearwater expecting the same general architecture (such as the Choptima handset, or NERD/DiveCAN HUD). It will also probably work (may require some small code changes around the IDs used) with rEvo and ISC shearwaters, but that hasn't been validated.

The current form factor is driven by the ease of manufacturing and RnD on the design rather than being fully optimized for widespread implementation. For example the battery has been put on the PCB because that is the easiest place for it to be secured. Wires and connectors are places for assembly defects and failures so I like to avoid them where possible. Having the battery on the PCB means that if the PCB is secure then the battery is secure, and the PCB has mounting holes, which makes securing it easier.

The long thin aspect ratio was driven by the ease of manufacturing of housings, the current form fits into a simple tube, with cables running out each end. One for CCR IO (cells, solenoid, etc) and DiveCAN out the other.

# Roadmap
### Current software jobs
These are required before the firmware can be considered ready for any kind of test diving:
- Menu logic for displaying any error states, as well as firmware commit
- IWDG task verify program state
- Error logging to eeprom emulation
- Work out why the brightness goes down when the CAN bus connects (protocol issue?)
- ADC intermittently doesn't come online after shutdown logic added, board reset required to fix.

### Current hardware issues (required on next board version)
- standby power consumption is 10 mA (should be low hundreds of micro amps at most)
    - This has been identified as coming from the DC-DC converter, perhaps this is a good opportunity to change to a wider input range
- Battery is not enabled without can-power, kind of defeats the point of a redundant power supply
- BATTERY ADC unavail due to internal mux, should route to PC3
- CAN_PWR only takes 1.8-5.5v, should be able to comfortably take 9v for compatibility with rEvo bus and 9v batteries.

### Additional testing needs
- More automated testing of each module, regression testing and behavior verification.
- Datalog captures from rEvos
- Verification of compatibility with monitor bus devices, ISC.

### Future Hardware Improvements (desired on next board version)
- CAN_EN should be routed to any of PA0, PC13, PE6, PA2, or PC5 so we can go to standby/shutdown rather than just STOP
- Add pulldowns on some spare pins (HW version identification, so we can maintain backwards compatibility with old boards)

### Future Software Improvements
- Battery reading and power switching 
- Menu for setting cell type
- Make DiveCAN multi-device safe so we can make multiple menu rows
- Predive "app", show loop pressure and such


### Prospective future developments
- Additional form-factors, shorter, more compact.
- DAC output to analog cell port, allow use of analog backup monitor with digital cells
- Break off battery to separate system, can be isolated physically for flood tolerance.