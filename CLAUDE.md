# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is firmware for a DiveCAN-compatible PPO2 monitoring and control head for closed-circuit rebreathers (CCRs). The system interfaces with oxygen sensors (both analog and digital), manages solenoid control via PID loop, and communicates with Shearwater dive computers over the DiveCAN bus.

The hardware is based on an STM32L4 microcontroller running FreeRTOS with dual-rail power (VCC for critical systems, VBUS for peripherals), external ADCs for analog oxygen sensors, UARTs for digital sensors, SD card logging, and CAN bus communication.

## Build Commands

### Firmware (STM32)

```bash
# Build firmware
cd STM32
make

# Build output: STM32/build/STM32.hex and STM32/build/STM32.bin
```

### Unit Tests (C/C++)

```bash
# First time setup - build cpputest
cd STM32/Tests/cpputest
autoreconf -i
./configure
make

# Build and run unit tests
cd STM32/Tests
make
```

### Hardware Tests (Python/pytest)

```bash
# Install dependencies
pip install -r requirements.txt

# Run hardware integration tests
cd "HW Testing/Tests"
pytest

# Run specific test file
pytest test_ppo2_control.py
```

## Flashing Firmware

### Serial + CAN (Recommended)
```bash
./flash_serial.sh
```
Requires USB-TTY adapter on RX2/TX2 and USB CAN adapter. Automatically enters bootloader via CAN, then flashes via serial (ttyUSB0).

### CAN Only
```bash
./flash_can.sh
```
Uses custom can-prog fork. Less reliable, may require manual reset.

## Code Architecture

### STM32 Firmware Structure

The firmware is organized into functional modules under `STM32/Core/Src/`:

**DiveCAN/** - DiveCAN protocol implementation
- `DiveCAN.c`: Main CAN message handling task, processes commands (ping, cal, setpoint, etc.)
- `Transciever.c`: Low-level CAN TX/RX operations
- `PPO2Transmitter.c`: Periodic PPO2 broadcast to dive computer
- `menu.c`: DiveCAN menu system for configuration

**Sensors/** - Oxygen sensor drivers
- `OxygenCell.c`: Generic cell interface and voting logic
- `AnalogOxygen.c`: Analog galvanic cell driver (via external ADC)
- `DiveO2.c`: DiveO2 digital cell driver (UART)
- `OxygenScientific.c`: O2S digital cell driver (UART)

**PPO2Control/** - Control system
- `PPO2Control.c`: PID controller for solenoid, manages setpoint and solenoid firing task

**Hardware/** - Hardware abstraction
- `ext_adc.c`: External dual differential ADC (ADS1115) via I2C
- `solenoid.c`: Solenoid driver (boost converter control)
- `pwr_management.c`: VCC/VBUS power rail management
- `flash.c`: EEPROM emulation for configuration persistence
- `log.c`: SD card logging (dive logs, events, errors)
- `printer.c`: Debug UART output
- `hw_version.c`: Hardware revision detection

**Core files:**
- `main.c`: Hardware initialization, FreeRTOS task creation
- `freertos.c`: FreeRTOS configuration and system tasks
- `configuration.c`: Configuration management (cell types, power modes, solenoid voltage)
- `errors.c`: Error tracking and reporting to DiveCAN

### FreeRTOS Task Architecture

The system runs multiple concurrent tasks:
- **CANTask**: Processes incoming DiveCAN messages, dispatches responses
- **PPO2ControllerTask**: PID loop, calculates solenoid duty cycle based on cell readings vs setpoint
- **SolenoidFireTask**: Manages solenoid firing timing based on duty cycle
- **PPO2TransmitterTask**: Broadcasts PPO2 to dive computer at 200ms intervals
- **OxygenCellTasks** (3x): Per-cell reading tasks for analog/digital sensors
- **watchdogTask**: Feeds hardware watchdog
- **SDInitTask**: Initializes SD card and filesystem

### Configuration System

Configuration is stored in flash via EEPROM emulation and passed between modules:
- Cell type configuration (3 bits: digital vs analog per cell)
- VBUS power mode (2 bits: always on, always off, auto)
- Solenoid voltage selection (1 bit: 12V vs 6V)

See `STM32/ConfigFormat.md` for bit layout.

### Testing Strategy

**Unit Tests (STM32/Tests/):**
- Uses CppUTest framework
- Mocks HAL/FreeRTOS dependencies (in `STM32/Mocks/`)
- Tests individual modules: PPO2Control, configuration, sensors

**Hardware Tests (HW Testing/Tests/):**
- Uses pytest with custom fixtures
- Requires hardware test stand with:
  - Arduino Due running HardwareShim (simulates cells via DACs)
  - CAN interface to DUT
  - Riden PSU for power cycling tests
- Tests full system integration: calibration, PPO2 control, power modes, menu system

## Key Implementation Patterns

### Error Handling
Errors are tracked in `errors.c` and reported to the dive computer via DiveCAN status messages. Fatal errors trigger system shutdown. The error system composes multiple error states (cell failures, low battery, calibration issues).

### Cell Voting
The system uses a voting algorithm across up to 3 oxygen cells. If cells disagree beyond thresholds, it reports cell failure to the dive computer. Implementation in `Sensors/OxygenCell.c`.

### Power Management
Dual-rail architecture:
- **VCC**: Always on when any power available (battery or CAN bus), runs MCU and critical systems
- **VBUS**: Controllable rail for sensors, can be powered from battery, CAN bus, or disabled to save power

Power source selection is automatic but mode is configurable (always enable VBUS for digital sensors, auto-disable for analog to save power, etc.).

### DiveCAN Protocol
Emulates a DiveCAN SOLO board (JJ CCR head). The protocol is message-based over CAN with specific message IDs for different functions. Key messages:
- Bus init handshake
- Ping/response
- Calibration commands
- Setpoint updates
- Atmospheric pressure (for depth compensation)
- Menu navigation
- Status/error reporting

## Development Notes

### NASA Rules of 10
This codebase follows NASA's Power of 10 rules for safety-critical embedded systems:
- No complex flow constructs (goto, recursion)
- All loops have fixed bounds
- No heap allocation (FreeRTOS uses static allocation)
- Functions limited to one page
- Runtime assertions in functions
- Minimal scope for variables
- Check return values or explicitly cast to void
- Sparing preprocessor use
- Limit pointer indirection
- Compile with all warnings enabled

### Known Issues (from STM32/Jobs.md)
- Power reset during start with SD card
- O2S intermittent garbled data
- ADC intermittent timeout
- Low voltage detection needs work

### Debugging
The system outputs debug information via SWD, which is done using the cortex-debug plugin.

Stack analysis available via `STM32/stackAnalysis.sh`.

## Subsystem Documentation

Detailed documentation for key subsystems is available in the `docs/` directory.

**Maintenance Directive:** When making changes to the codebase that affect documented subsystems, update the corresponding documentation in `docs/`. This includes:
- Adding/removing/renaming DIDs, services, or settings
- Changing data structures (Configuration_t, state DIDs, etc.)
- Modifying FreeRTOS task priorities or patterns
- Updating calibration methods or voting algorithms
- Changes to the testing infrastructure or fixtures
- Protocol changes (UDS, ISO-TP, SLIP)

Documentation should stay in sync with the implementation to reduce future exploration time.

| Document | Description |
|----------|-------------|
| [docs/UDS_PROTOCOL.md](docs/UDS_PROTOCOL.md) | UDS diagnostic services (0x10, 0x22, 0x2E, 0x34-37), session model, NRCs |
| [docs/ISOTP_TRANSPORT.md](docs/ISOTP_TRANSPORT.md) | ISO-TP framing, state machine, TX queue, timeouts |
| [docs/DATA_IDENTIFIERS.md](docs/DATA_IDENTIFIERS.md) | Complete DID reference (0xF2xx state, 0xF4Nx cells, 0x9xxx settings) |
| [docs/FREERTOS_PATTERNS.md](docs/FREERTOS_PATTERNS.md) | Cooperative scheduling, 1-element peek queues, static allocation |
| [docs/OXYGEN_SENSORS.md](docs/OXYGEN_SENSORS.md) | Analog/DiveO2/O2S drivers, voting algorithm, calibration methods |
| [docs/CONFIGURATION_SYSTEM.md](docs/CONFIGURATION_SYSTEM.md) | Configuration_t bitfield, settings via UDS, flash persistence |
| [docs/TESTING_ARCHITECTURE.md](docs/TESTING_ARCHITECTURE.md) | DiveCANpy, pytest fixtures, HWShim commands, PSU control |
| [docs/DIVECAN_BT.md](docs/DIVECAN_BT.md) | Browser JS client, SLIP encoding, UDSClient API |

### External References

- [DiveCAN Protocol](https://github.com/QuickRecon/DiveCAN) - Base protocol documentation and message format