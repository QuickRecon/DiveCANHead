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
- No complex flow constructs (goto, recursion). Note: single-return pattern (c:S1142) is the project convention — use result variables, not early returns.
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

### SonarQube Integration

This project uses SonarQube Cloud for static analysis. Issues are available through two channels:

**1. IDE Diagnostics (Real-time)**
When editing files, SonarQube warnings appear in `<new-diagnostics>` tags within `<system-reminder>` blocks attached to tool results. These reflect the current state of open files.

**2. MCP Server (Project-wide)**
Use the SonarQube MCP tools to query the server directly:

```
# List available projects
mcp__sonarqube__search_my_sonarqube_projects

# Search for issues (project key: QuickRecon_DiveCANHead)
mcp__sonarqube__search_sonar_issues_in_projects with projects: ["QuickRecon_DiveCANHead"]

# Get details about a specific rule
mcp__sonarqube__show_rule with key: "c:S109"
```

**3. IDE MCP Diagnostics (Comprehensive)**
Get all current diagnostics including SonarQube warnings via the IDE MCP tool:

```
mcp__ide__getDiagnostics
```

This returns diagnostics for all open/analyzed files. Filter for specific files using jq:
```bash
# Extract UDS-related diagnostics
cat <output_file> | jq -r '.[0].text' | jq '[.[] | select(.uri | test("uds"))]'

# Count issues by rule type
cat <output_file> | jq -r '.[] | .diagnostics[] | select(.source == "sonarqube") | .code' | sort | uniq -c
```

**Common Rule IDs:**
| Rule | Description | Fix |
|------|-------------|-----|
| c:S109 | Magic number | Define named constant |
| c:S787 | C99-style comment | Use `/* */` not `//` |
| c:S868 | Complex operand | Add parentheses |
| c:S813 | Raw float type | Use typedef (e.g., `PPO2_t`) |
| c:S116 | Field naming | Use camelCase for struct fields |
| c:S1774 | Ternary operator | Use if/else |
| c:M23_007 | Unused return value | Assign or cast to `(void)` |
| c:M23_388 | Non-const global | Make const or pass as parameter |

**Important Workflow Notes:**
- SonarCloud only analyzes pushed commits. Local changes show in IDE diagnostics but won't appear in server queries until pushed.
- **Diagnostics disappear after first read:** When `<new-diagnostics>` is called, the diagnostics are consumed and won't appear in `<new-diagnostics>` tags on subsequent tool calls. Save results to a persistent file immediately:
  ```bash
  # Save to .sonarqube-diagnostics.json in the relevant directory
  cat <output_file> | jq '.[0].text' > path/to/.sonarqube-diagnostics.json
  ```
- During long sessions or before context compaction, re-run `mcp__ide__getDiagnostics` and save fresh results to ensure diagnostics remain available. Compare with existing issues to detect duplicates/non-existant old issues.

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

## Code Style Guide

**Maintenance Directive:** When fixing SonarQube issues, add new patterns to this style guide to prevent recurrence. This guide should grow based on actual findings - if you fix an issue pattern not documented here, add it.

### Constants and Magic Numbers (c:S109)

**Rule:** All numeric literals (except 0, 1, and -1) must be named constants.

**Implementation:**
- Use `static const` for constants (not `#define`) unless the value is needed in:
  - Switch case statements (must be compile-time constant in C)
  - Array size declarations
  - Preprocessor conditionals
- Place constants in the most appropriate scope:
  - `common.h` for project-wide constants (byte widths, masks, cell counts)
  - Module header for module-specific constants (e.g., `isotp.h` for ISO-TP frame sizes)
  - Source file for file-local constants (setting indices, queue sizes)

**Existing Constants (always check before defining new ones):**
```c
/* In common.h */
static const uint32_t BYTE_WIDTH = 8;        /* For bit shifts */
static const uint32_t TWO_BYTE_WIDTH = 16;   /* For bit shifts */
static const uint32_t THREE_BYTE_WIDTH = 24; /* For bit shifts */
static const uint8_t BYTE_MASK = 0xFFU;      /* Byte extraction mask */
static const uint8_t CELL_COUNT = 3;         /* Number of oxygen cells */

/* In isotp.h */
static const size_t CAN_FRAME_LENGTH = 8U;
static const size_t ISOTP_SF_MAX_DATA = 7U;
static const size_t ISOTP_FF_DATA_BYTES = 6U;
static const size_t ISOTP_CF_DATA_BYTES = 7U;
static const uint8_t ISOTP_SEQ_MASK = 0x0FU;
static const uint8_t ISOTP_BROADCAST_ADDR = 0xFFU;
```

**Naming conventions for new constants:**
- Byte sizes/widths: `*_SIZE`, `*_LENGTH`, `*_WIDTH`
- Array indices: `*_IDX`
- Bit masks: `*_MASK`
- Start positions: `*_START`, `*_OFFSET`

### Ternary Operators (c:S1774)

**Rule:** Avoid ternary operators. Use explicit if/else blocks.

**Bad:**
```c
uint8_t val = (condition) ? 7 : 0;
```

**Good:**
```c
uint8_t val;
if (condition)
{
    val = SOME_CONSTANT;
}
else
{
    val = 0;
}
```

### Variable Initialization (c:M23_321)

**Rule:** Initialize local variables at declaration when possible.

**Bad:**
```c
uint8_t bytesToCopy;
if (remaining > 7)
{
    bytesToCopy = 7;
}
```

**Good:**
```c
uint8_t bytesToCopy = 0;
if (remaining > ISOTP_CF_DATA_BYTES)
{
    bytesToCopy = ISOTP_CF_DATA_BYTES;
}
```

### Return Value Handling (c:M23_007)

**Rule:** All return values must be used or explicitly cast to `(void)`.

```c
(void)memcpy(dest, src, len);
(void)memset(buf, 0, sizeof(buf));
```

### Global Variables (c:M23_388)

**Rule:** Global variables should be `const` where possible. If state must be mutable, encapsulate in a static struct.

**Bad:**
```c
uint8_t globalBuffer[256];
```

**Good:**
```c
static struct {
    uint8_t buffer[256];
    uint16_t length;
} moduleState = {0};
```

### Complex Operands (c:S868)

**Rule:** Add explicit parentheses around complex bitwise expressions.

**Bad:**
```c
value = a << 8 | b;
```

**Good:**
```c
value = (a << BYTE_WIDTH) | b;
```

### Include Order

1. Module's own header (e.g., `uds_settings.h`)
2. Other module headers (e.g., `isotp.h`)
3. Project common headers (e.g., `../../common.h`)
4. External library headers (e.g., `cmsis_os.h`)
5. Standard library headers (e.g., `<string.h>`)

### Type Widths

- Always use explicit width types: `uint8_t`, `uint16_t`, `uint32_t`, `int16_t`, etc.
- Use typedefs from `common.h` for domain values: `PPO2_t`, `Millivolts_t`, `Timestamp_t`
- Add `U` suffix to unsigned literals: `0xFFU`, `8U`, `256U`

### Switch Case Labels

PCI values and other switch case labels must use `#define` (not `static const`) because C requires compile-time constant expressions in case labels:

```c
/* Must be #define for switch cases */
#define ISOTP_PCI_SF 0x00U
#define ISOTP_PCI_FF 0x10U

switch (pci)
{
case ISOTP_PCI_SF:  /* Works */
    break;
}
```

### Nested If-Else for Error Handling (c:S1005, c:S1142)

**Rule:** Functions should have a single return statement. Use nested if-else where error paths are in `if` and success paths are in `else`.

**Bad:**
```c
bool foo(Context_t *ctx) {
    if (ctx == NULL) {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return false;
    }
    /* ... success path ... */
    return true;
}
```

**Good:**
```c
bool foo(Context_t *ctx) {
    bool result = false;
    if (ctx == NULL)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
    }
    else
    {
        /* ... success path ... */
        result = true;
    }
    return result;
}
```

### Static Accessor Methods for Module State (c:M23_388)

**Rule:** Global state should be scoped to file via static accessor functions. This encapsulates state and satisfies the rule about non-const globals.

**Bad:**
```c
static ISOTPTxState_t txState = {0};
static osMessageQueueId_t txQueueHandle = NULL;
```

**Good:**
```c
static ISOTPTxState_t *getTxState(void)
{
    static ISOTPTxState_t txState = {0};
    return &txState;
}

static osMessageQueueId_t *getTxQueueHandle(void)
{
    static osMessageQueueId_t handle = NULL;
    return &handle;
}

/* Usage */
ISOTPTxState_t *state = getTxState();
state->txActive = true;
```

### Struct Initialization (c:S6871)

**Rule:** When initializing structs, explicitly initialize all fields rather than relying on default initialization.

**Bad:**
```c
const osMessageQueueAttr_t queueAttr = {
    .name = "MyQueue",
    .cb_mem = &controlBlock,
    .cb_size = sizeof(controlBlock),
    .mq_mem = storage,
    .mq_size = sizeof(storage)};
```

**Good:**
```c
const osMessageQueueAttr_t queueAttr = {
    .name = "MyQueue",
    .attr_bits = 0,
    .cb_mem = &controlBlock,
    .cb_size = sizeof(controlBlock),
    .mq_mem = storage,
    .mq_size = sizeof(storage)};
```

### If-Else-If Chains (c:M23_112, c:S126)

**Rule:** All if-else-if chains must be terminated with a final else clause.

**Bad:**
```c
if (condition1)
{
    /* ... */
}
else if (condition2)
{
    /* ... */
}
```

**Good:**
```c
if (condition1)
{
    /* ... */
}
else if (condition2)
{
    /* ... */
}
else
{
    /* No action required */
}
```

### Merge Collapsible If Statements (c:S1066)

**Rule:** When an if statement is the only statement inside another if, merge them with `&&`.

**Bad:**
```c
if (condition1)
{
    if (condition2)
    {
        /* ... */
    }
}
```

**Good:**
```c
if (condition1 && condition2)
{
    /* ... */
}
```

### Extern Declarations (c:S824)

**Rule:** Function declarations with `extern` should be placed at file scope (top of file, after includes), not inside function bodies.

**Bad:**
```c
void MyFunction(void)
{
    extern bool OtherModule_IsBusy(void);
    if (OtherModule_IsBusy())
    {
        /* ... */
    }
}
```

**Good:**
```c
/* At file top, after includes */
extern bool OtherModule_IsBusy(void);

void MyFunction(void)
{
    if (OtherModule_IsBusy())
    {
        /* ... */
    }
}
```

### Identifier Length (c:S799)

**Rule:** Identifiers must be <=31 characters for portability.

**Bad:**
```c
UDS_NRC_INCORRECT_MESSAGE_LENGTH = 0x13,  /* 32 chars */
```

**Good:**
```c
UDS_NRC_INCORRECT_MSG_LEN = 0x13,  /* 25 chars */
```