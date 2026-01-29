# Testing Architecture

This document describes the testing infrastructure including DiveCANpy, pytest fixtures, and hardware integration testing.

## Overview

The project uses two testing approaches:
1. **Unit Tests** - CppUTest framework for isolated module testing
2. **Hardware Tests** - pytest with real hardware for integration testing

## Source Files

- `STM32/Tests/` - Unit tests (CppUTest)
- `HW Testing/Tests/` - Hardware integration tests (pytest)
- `DiveCANpy/` - Python DiveCAN interface library

## DiveCANpy Library

Python library for communicating with DUT over CAN bus.

### Files

| File | Purpose |
|------|---------|
| `DiveCANpy/DiveCAN.py` | Core CAN message interface |
| `DiveCANpy/bootloader.py` | STM32 bootloader operations |
| `DiveCANpy/configuration.py` | Configuration management |

### DiveCAN Class

```python
class DiveCAN:
    def __init__(self, device: str) -> None:
        """Initialize CAN interface via SLCAN adapter"""
        self._bus = can.interfaces.slcan.slcanBus(
            channel=device, bitrate=125000, timeout=0.1, rtscts=True
        )

    # Message sending
    def send_bootloader(self) -> None
    def send_setpoint(self, src_id: int, setpoint: int)
    def send_calibrate(self) -> None
    def send_menu_req(self, target_id: int, src_id: int) -> None

    # Message receiving
    def listen_for_ppo2(self) -> can.Message
    def listen_for_status(self) -> can.Message
    def listen_for_cal(self) -> can.Message
    def listen_for_millis(self) -> can.Message
```

### Usage Example

```python
from DiveCANpy import DiveCAN

client = DiveCAN.DiveCAN('/dev/ttyCAN0')
client.send_id(1)  # Send bus init
msg = client.listen_for_ppo2()  # Wait for PPO2 response
print(f"Cell 1 PPO2: {msg.data[0]}")
client.stop()
```

## pytest Fixtures

Defined in `HW Testing/Tests/conftest.py`:

### Basic Fixtures

```python
@pytest.fixture()
def power_divecan_client_fixture():
    """DiveCAN interface with PSU control"""
    power = psu.PSU()
    divecan_client = make_divecan()
    yield (divecan_client, power)
    divecan_client.stop()

@pytest.fixture()
def power_shim_divecan_fixture():
    """DiveCAN + HWShim + PSU"""
    power = psu.PSU()
    divecan_client = make_divecan()
    yield (divecan_client, HWShim.HWShim(), power)
    divecan_client.stop()
```

### Parameterized Fixtures

```python
@pytest.fixture(params=configuration.supported_configurations())
def config_and_power_divecan_client(request):
    """DiveCAN with configuration matrix"""
    pwr = psu.PSU()
    divecan_client = make_divecan()
    shim_host = HWShim.HWShim()
    configuration.configure_board(divecan_client, request.param)
    yield (divecan_client, shim_host, request.param, pwr)
    divecan_client.stop()

@pytest.fixture(params=configuration.supported_configurations())
def config_and_cal_and_power_divecan_client(request):
    """DiveCAN with configuration and calibration"""
    # ... configure and calibrate ...
    utils.ensureCalibrated(divecan_client, shim_host)
    yield (divecan_client, shim_host, request.param, pwr)
```

## Hardware Shim (HWShim)

Arduino Due-based hardware simulator for injecting sensor values.

### File: `HW Testing/Tests/HWShim.py`

```python
class HWShim:
    def __init__(self) -> None:
        self._serial_port = serial.Serial('/dev/ttyDUE0', 115200)

    def set_digital_ppo2(self, cell_num: int, ppo2: float) -> None:
        """Set digital cell PPO2 (DiveO2/O2S simulation)"""
        msg = f"sdc,{cell_num},{ppo2*100},"
        self._serial_port.write(msg.encode())
        # Wait for acknowledgment...

    def set_analog_millis(self, cell_num: int, millis: float) -> None:
        """Set analog cell millivolts (DAC output)"""
        msg = f"sac,{cell_num},{millis},"
        self._serial_port.write(msg.encode())

    def set_digital_mode(self, cell_num: int, mode: ShimDigitalCellType):
        """Set digital cell protocol (DiveO2 vs O2S)"""
        msg = f"scm,{cell_num},{int(mode)},"
        self._serial_port.write(msg.encode())

    def set_bus_on(self) -> None:
        """Enable CAN bus power"""

    def set_bus_off(self) -> None:
        """Disable CAN bus power"""
```

### HWShim Commands

| Command | Format | Description |
|---------|--------|-------------|
| sdc | `sdc,N,ppo2,` | Set digital cell N PPO2 (centibar) |
| sac | `sac,N,mV,` | Set analog cell N millivolts |
| scm | `scm,N,mode,` | Set cell N mode (0=DiveO2, 1=O2S) |
| sdcen | `sdcen` | Enable DC bus |
| sdcden | `sdcden` | Disable DC bus |

## PSU Control

`HW Testing/Tests/psu.py` controls the lab power supply:

### Supported PSUs

- **Riden RD6006** - Primary PSU over serial
- **Nordic PPK2** - For power profiling

```python
class PSU:
    def __init__(self):
        # Initialize Riden PSU
        pass

    def set_voltage(self, voltage: float):
        """Set output voltage"""

    def set_current_limit(self, current: float):
        """Set current limit"""

    def enable_output(self):
        """Enable PSU output"""

    def disable_output(self):
        """Disable PSU output"""
```

## Device Paths

Standard device paths for test hardware:

| Device | Path | Description |
|--------|------|-------------|
| CAN Adapter | `/dev/ttyCAN0` | USB-SLCAN adapter |
| Arduino Due | `/dev/ttyDUE0` | HWShim controller |
| Riden PSU | `/dev/ttyUSB0` | Lab power supply |

These are typically set up via udev rules for consistent naming.

## Test Organization

### Test Files

| File | Purpose |
|------|---------|
| `test_ping.py` | Basic connectivity tests |
| `test_ppo2.py` | PPO2 reading accuracy |
| `test_ppo2_control.py` | Solenoid control loop |
| `test_calibration.py` | Calibration procedures |
| `test_configuration.py` | Configuration persistence |
| `test_pwr_management.py` | Power mode switching |
| `test_menu.py` | DiveCAN menu system |
| `test_bootloader.py` | Firmware update |

### Running Tests

```bash
cd "HW Testing/Tests"

# Run all tests
pytest

# Run specific test file
pytest test_ppo2_control.py

# Run with verbose output
pytest -v

# Run specific test
pytest test_ppo2.py::test_cell_reading_accuracy
```

## Test Patterns

### Basic Test

```python
def test_ppo2_reading(power_shim_divecan_fixture):
    divecan, shim, psu = power_shim_divecan_fixture

    # Set known PPO2 on shim
    shim.set_digital_ppo2(0, 0.21)

    # Read from DUT
    msg = divecan.listen_for_ppo2()

    # Verify
    assert msg.data[0] == 21  # 0.21 bar = 21 centibar
```

### Parameterized Test

```python
@pytest.mark.parametrize("config", configuration.supported_configurations())
def test_config_persistence(power_divecan_client_fixture, config):
    divecan, psu = power_divecan_client_fixture

    # Apply configuration
    configuration.configure_board(divecan, config)

    # Power cycle
    psu.disable_output()
    time.sleep(1)
    psu.enable_output()

    # Verify configuration persisted
    # ...
```

### Configuration Matrix

```python
# From configuration.py
def supported_configurations():
    """Return list of valid configuration combinations"""
    return [
        Config(cell1=ANALOG, cell2=ANALOG, cell3=ANALOG),
        Config(cell1=DIVEO2, cell2=ANALOG, cell3=ANALOG),
        Config(cell1=DIVEO2, cell2=DIVEO2, cell3=ANALOG),
        # ...
    ]
```

## Unit Tests (CppUTest)

### Building Unit Tests

```bash
# First time: build cpputest
cd STM32/Tests/cpputest
autoreconf -i
./configure
make

# Build and run tests
cd STM32/Tests
make
```

### Mock Structure

Mocks are in `STM32/Mocks/`:
- HAL functions (GPIO, I2C, UART, etc.)
- FreeRTOS functions (queues, tasks, etc.)
- Peripheral drivers

### Test Example

```cpp
TEST(PPO2Control, SetpointTracking)
{
    // Setup
    PPO2Control_Init(&config);

    // Exercise
    PPO2Control_SetSetpoint(1.0f);
    PPO2Control_Update(0.8f);  // Below setpoint

    // Verify
    CHECK(PPO2Control_GetDutyCycle() > 0.0f);
}
```
