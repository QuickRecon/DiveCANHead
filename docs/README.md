# DiveCANHead Documentation Index

This directory contains detailed subsystem documentation for the DiveCANHead firmware project.

## Quick Reference

| Document | Description |
|----------|-------------|
| [UDS_PROTOCOL.md](UDS_PROTOCOL.md) | UDS diagnostic services, session model, NRCs |
| [ISOTP_TRANSPORT.md](ISOTP_TRANSPORT.md) | ISO-TP framing, state machine, TX queue |
| [DATA_IDENTIFIERS.md](DATA_IDENTIFIERS.md) | Complete DID reference (state, cells, settings) |
| [FREERTOS_PATTERNS.md](FREERTOS_PATTERNS.md) | Cooperative scheduling, queues, static allocation |
| [OXYGEN_SENSORS.md](OXYGEN_SENSORS.md) | Sensor drivers, voting algorithm, calibration |
| [CONFIGURATION_SYSTEM.md](CONFIGURATION_SYSTEM.md) | Configuration_t bitfield, persistence |
| [TESTING_ARCHITECTURE.md](TESTING_ARCHITECTURE.md) | DiveCANpy, pytest fixtures, HWShim |
| [DIVECAN_BT.md](DIVECAN_BT.md) | Browser JS client, SLIP, UDSClient |
| [FLASHING_FIRMWARE.md](FLASHING_FIRMWARE.md) | SWD flashing via STM32CubeProgrammer |

## External References

- [DiveCAN Protocol](https://github.com/QuickRecon/DiveCAN) - Base protocol documentation
- [ISO 14229 (UDS)](https://www.iso.org/standard/72439.html) - Unified Diagnostic Services specification
- [ISO 15765-2 (ISO-TP)](https://www.iso.org/standard/66574.html) - Transport protocol specification

## Source File Mapping

| Subsystem | Key Source Files |
|-----------|------------------|
| UDS | `STM32/Core/Src/DiveCAN/uds/uds.c`, `uds.h` |
| ISO-TP | `STM32/Core/Src/DiveCAN/uds/isotp.c`, `isotp_tx_queue.c` |
| DIDs | `uds_state_did.h`, `uds_settings.c`, `DiveCAN_bt/src/uds/constants.js` |
| FreeRTOS | `STM32/Core/Inc/FreeRTOSConfig.h`, `main.c` |
| Sensors | `Sensors/OxygenCell.c`, `AnalogOxygen.c`, `DiveO2.c`, `OxygenScientific.c` |
| Config | `configuration.h`, `configuration.c`, `ConfigFormat.md` |
| Testing | `DiveCANpy/*.py`, `HW Testing/Tests/*.py` |
| BT Client | `DiveCAN_bt/src/**/*.js` |
