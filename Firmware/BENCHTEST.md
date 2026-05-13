# DiveCAN Jr Hardware-in-the-Loop Test Checklist

Manual hardware bench tests required before merging changes to the
OTA, power, or boot subsystems. Each entry is structured so it can be
lifted into the `tests/integration/` pytest harness later —
preconditions, actions, and pass criteria are expressed in concrete,
scriptable terms.

The existing integration harness (Arduino Due cell-shim, Riden PSU
control, CAN dongle) supplies most of the plumbing needed to automate
each entry. Until that automation exists, these are run by hand
during phase merges and the results recorded in the PR description.

**Maintenance directive:** when adding or modifying a safety-critical
behaviour, add or update the corresponding entry here in the same PR.
Same convention as `ARCHITECTURE.md` and `COMPROMISE.md`.

---

## Section 1 — Boot & power (Phase 1)

### BT-1.1 Cold-boot from clean flash

- **Pre:** Unit freshly programmed via `flash.sh` (MCUBoot + signed
  app via sysbuild merged hex). External NOR fully erased
  (`flash_erase 0 64M` on the SPI NOR via probe).
- **Action:** Power on. Observe heartbeat LED and RTT log.
- **Pass:**
  - MCUBoot startup messages on RTT within 1 s.
  - Heartbeat LED blinks during MCUBoot's pre-boot window
    (driven by `CONFIG_MCUBOOT_INDICATION_LED`).
  - App reaches its main loop within 3 s of power-on.
  - App's heartbeat thread takes over the LED.
  - No errors in MCUBoot output relating to slot1/scratch
    (acceptable: "no image in secondary slot" — slot is empty).

### BT-1.2 Settings persistence across reboot

- **Pre:** BT-1.1 passed. Unit is running normally.
- **Action:**
  1. Write a known runtime setting via UDS (e.g. PPO2 control mode
     to a non-default value).
  2. Power-cycle.
  3. Read the setting back via UDS.
- **Pass:** The setting matches what was written. Confirms NVS
  storage on external NOR is functional.

### BT-1.3 Mid-erase power pull on external NOR

- **Pre:** Begin a `flash_erase` operation on the slot1 partition
  (192–224 KB region). Pull power at ~50 % of the erase.
- **Action:** Restore power. Observe boot.
- **Pass:**
  - Unit boots from slot0 (running app).
  - No MCUBoot stall — partial-erased slot1 may report "no image"
    or "invalid header" in RTT, both acceptable.
  - App reaches main loop.
  - No persistent fault state in error histogram (0xF260).

### BT-1.4 IWDG survival across MCUBoot startup

- **Pre:** Soft-reset the unit via `sys_reboot()` (e.g. from a UDS
  command in a confirmed image).
- **Action:** Observe boot.
- **Pass:**
  - IWDG (8 s window) does NOT trip during MCUBoot's hash walk +
    swap-state read.
  - App reaches its main loop within the same ~3 s budget as
    BT-1.1.
  - Implicit confirmation of `CONFIG_BOOT_WATCHDOG_FEED=y` working.

### BT-1.5 Heartbeat-LED handoff

- **Pre:** Unit running normally for ≥30 s.
- **Action:** Visually inspect heartbeat LED behaviour across a
  power cycle.
- **Pass:**
  - During MCUBoot window: LED blinks at MCUBoot's
    indication-LED cadence (typically faster than the app's
    heartbeat).
  - After app boot: LED blinks at the app's heartbeat cadence.
  - Smooth handoff, no extended dark period.

---

## Section 2 — OTA transfer (Phase 3, placeholder)

_Filled in when Phase 3 lands. Will cover RequestDownload (0x34),
TransferData (0x36), RequestTransferExit (0x37), header rejection,
hash rejection at activation._

## Section 3 — POST + confirm (Phase 4, placeholder)

_Filled in when Phase 4 lands. Will cover handset-present and
handset-absent scenarios, cell-failure rollback, consensus-failure
rollback, PPO2-TX failure rollback, overall deadline timeout._

## Section 4 — Factory backup (Phase 5, placeholder)

_Filled in when Phase 5 lands. Will cover first-boot capture,
verify-after-write, force-recapture, restore-from-factory._

## Section 5 — UDS OTA diagnostics (Phase 6, placeholder)

_Filled in when Phase 6 lands. Will cover read DIDs 0xF270–0xF274
and write DIDs 0xF275–0xF277._

## Section 6 — Backport (Phase 7, separate ticket)

_SD-card-equipped older hardware: full OTA cycle via the
flash-disk-wrapper driver, SD-removal-during-OTA fault injection,
FAT corruption recovery._
