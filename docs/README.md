# DiveCAN Head documentation

The active application is the Zephyr firmware in `Firmware/`. Start with the
[root README](../README.md) for workspace setup, boards, variants, and builds.

## Current guides

| Document | Scope |
|---|---|
| [Development setup](DEVELOPMENT_SETUP.md) | Public dependencies, Python environment, SDK installation, first build, and troubleshooting |
| [Architecture](../Firmware/ARCHITECTURE.md) | Jr/MainPCB topology, storage, sensors, power, control, and threads |
| [Flashing firmware](FLASHING_FIRMWARE.md) | MCUboot/application image selection and SWD workflow |
| [Testing](TESTING_ARCHITECTURE.md) | Native tests, simulator integration, coverage, and physical HIL |
| [HIL GitHub Actions](HIL_GITHUB_ACTIONS.md) | Out-of-tree physical bench and qualification workflow |
| [Releasing](../Firmware/docs/RELEASING.md) | Numbered releases, qualification, and packages |
| [UDS wire reference](../Firmware/UDS.md) | Current services, sessions, transport behavior, and OTA |
| [Data identifiers](DATA_IDENTIFIERS.md) | Zephyr DID and routine catalogue |
| [State machines](../Firmware/docs/STATE_MACHINES.md) | Current state-machine design |
| [Flash log](../Firmware/docs/FLASH_LOG.md) | Persistent telemetry/text storage and retrieval |
| [Telemetry viewer](TELEMETRY_VIEWER.md) | Browser viewer and Python log parser |
| [Browser client](../DiveCAN_bt/README.md) | BLE/SLIP/ISO-TP/UDS API and examples |
| [Bluetooth internals](DIVECAN_BT.md) | Browser protocol stack |
| [Bench checklist](../Firmware/BENCHTEST.md) | Manual scenarios; not a record of completed qualification |
| [Compromises](../Firmware/COMPROMISE.md) | Engineering constraints and mitigations |

## Current source map

Paths below are relative to the repository root.

| Subsystem | Source |
|---|---|
| Board wiring | `Firmware/boards/quickrecon/` |
| Fitted topology and defaults | `Firmware/variants/` |
| UDS and ISO-TP | `Firmware/src/divecan/uds/` |
| Runtime settings | `Firmware/src/runtime_settings.c`, `Firmware/include/runtime_settings.h` |
| Oxygen sensors | `Firmware/src/oxygen_cell_analog.c`, `oxygen_cell_diveo2.c`, `oxygen_cell_o2s.c`, `oxygen_cell_math.c` |
| Tank pressure | `Firmware/src/tank_pressure.c`, `Firmware/src/tank_pressure_math.c` |
| Native tests | `Firmware/tests/`, `Firmware/scripts/native_test.py` |
| Simulator integration | `Firmware/tests/integration/harness/` |
| Browser tools | `DiveCAN_bt/src/`, `DiveCAN_bt/examples/` |
