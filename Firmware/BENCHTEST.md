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

### BT-1.6 Signed slot0 image validates on every boot (Phase 2)

- **Pre:** Unit programmed via `flash.sh` (sysbuild merged hex
  containing MCUBoot + an imgtool-signed v0.0.0+0 app). MCUBoot
  Kconfig has `CONFIG_BOOT_VALIDATE_SLOT0=y` (verify via
  `grep BOOT_VALIDATE build/mcuboot/zephyr/.config`).
- **Action:** Cold power-cycle and observe RTT log up to the app
  banner.
- **Pass:**
  - MCUBoot logs a successful slot0 validation pass (no
    `Image in the primary slot is not valid!` line).
  - App banner `DiveCAN Jr — Zephyr <version>` appears.
  - SHA-256 walk completes in well under the 8 s IWDG window —
    confirms `BOOT_WATCHDOG_FEED=y` is alive during the hash.
- **Notes:** Image layout reference (from `scripts/sign_app.py`
  output): 512 B header + body + 40 B TLV (single SHA-256 entry,
  TLV type 0x10). `python3 imgtool.py verify` against the
  signed bin should print `Image was correctly validated`.

### BT-1.7 Corrupt byte in slot0 → MCUBoot refuses to jump (Phase 2)

- **Pre:** Unit running normally from a known-good signed image.
  External NOR / slot1 state irrelevant.
- **Action:** Run `scripts/test_mcuboot_reject.sh`. The script:
  1. Copies the freshly-built `zephyr.signed.bin`, flips one byte
     at offset 0x1000 (past the 512 B header, before the TLV).
  2. Reflashes only slot0 at `0x08010000` via STM32CubeProgrammer —
     MCUBoot itself is untouched.
  3. Halts the chip via openocd, reads PC, dumps the SEGGER RTT
     up-buffer in RAM (`pBuffer`, `WrOff` from the control block).
  4. Prints the captured MCUBoot log + PASS/FAIL verdict.
- **Pass:**
  - Script prints **`PASS: MCUBoot rejected the corrupt image`**.
  - PC at halt is inside MCUBoot text (`< 0x08010000`).
  - When MCUBoot reaches its FIH_PANIC log path, the RTT buffer
    contains the textbook chain:
    `Image in the primary slot is not valid!` → `Unable to find
    bootable image`. (On the early-validation path the chip
    panics through `arch_system_halt` before that log line gets
    a chance — the PC check is still the authoritative verdict.)
  - Heartbeat LED does **not** transition to the app's cadence.
- **Recovery:** `./flash.sh` (the script prints this on exit too).
- **Notes:**
  - We dump the RTT up-buffer directly from RAM via openocd's
    `mdb` rather than running the live `rtt server` bridge.
    MCUBoot's writes happen in tens of ms during early boot and
    are long over by the time openocd's RTT poll thread finds
    the control block. The buffer-dump method is deterministic
    and avoids the host-attach race.
  - This is the most important defensive property of the
    bootloader and must remain green after any MCUBoot config
    change.
  - The pre-existing `E: Watchdog install timeout failed: -22`
    line in MCUBoot's output is unrelated — it fires whenever
    the L4 IWDG is already running from a prior boot, which is
    expected on warm-reset and harmless here (`BOOT_WATCHDOG_FEED`
    keeps the running IWDG fed regardless).

---

## Section 2 — OTA transfer (Phase 3)

The wire-protocol behaviour for SIDs 0x10 / 0x34 / 0x36 / 0x37 / 0x31 is
covered automatically by:

- **`tests/uds_ota/`** — native_sim ztest suite, 25 cases (run with
  `scripts/native_test.py run uds_ota`)
- **`tests/integration/harness/test_uds_ota.py`** — pytest E2E suite,
  8 cases over vcan against the real Zephyr firmware (run with
  `pytest test_uds_ota.py` from the harness venv)

The bench tests below catch what the host harness can't: the real
MCUBoot bootloader doing the actual swap on STM32L4 silicon. Until the
"Enable MCUBoot E2E on native_sim" follow-up lands, this section is the
authoritative coverage for the swap path.

### BT-2.1 Happy path: streamed image swaps in

- **Pre:** Unit running a confirmed v1 image. SD card / external NOR in
  whatever state — slot1 contents irrelevant.
- **Action:**
  1. Use the existing `tests/integration/harness/uds_ota.py` helpers (or
     equivalent dive-computer tool) over a USB CAN adapter:
     ```
     ota.enter_programming()
     ota.request_download(len(image))
     ota.transfer_image(image)
     ota.request_transfer_exit()
     ota.routine_activate()
     ```
  2. Wait ~15 s for MCUBoot's swap-using-scratch to complete.
  3. Power-cycle (or watch the reboot fire from the activate response).
- **Pass:**
  - Each UDS request gets a positive response (0x50 / 0x74 / 0x76 / 0x77
    / 0x71).
  - On the next boot, the running firmware is v2 (query DID 0xF000 to
    confirm).
  - No `Image in the primary slot is not valid!` line in MCUBoot's RTT
    log.
  - **The unit is now booting from a test-mode image — Phase 4 POST
    confirmation hasn't landed yet, so the image will revert on the
    next reboot unless `boot_write_img_confirmed()` is called manually
    via SWD.** This is the documented Phase 3 → Phase 4 gap; flagged in
    the OTA plan.

### BT-2.2 Header rejection at 0x37

- **Pre:** Unit running a confirmed image.
- **Action:** Run `tests/integration/harness/test_uds_ota.py`'s
  `truncate_image_header()` helper to zero the MCUBoot magic, then
  stream that corrupted image via 0x34 / 0x36 / 0x37.
- **Pass:**
  - 0x37 returns NRC (0x22 or 0x72).
  - slot0 is **untouched** — `mcuboot_swap_type()` reports
    BOOT_SWAP_TYPE_NONE on next read.
  - `boot_request_upgrade` was never called.

### BT-2.3 Hash rejection at 0x31 activate

- **Pre:** Unit running a confirmed image.
- **Action:** Use `corrupt_signed_image()` to flip a byte inside the
  body of a valid signed image. Stream the full 0x34/0x36/0x37/0x31
  sequence.
- **Pass:**
  - 0x37 succeeds (header magic + size still valid).
  - 0x31 returns NRC 0x22 (CONDITIONS_NOT_CORRECT) — the firmware-side
    SHA-256 walk caught the corruption before the bootloader was
    invoked.
  - `boot_request_upgrade` was never called.
  - The unit stays alive, no reboot.

### BT-2.4 Programming session refused mid-dive

- **Pre:** Unit running a confirmed image. Pressure transducer
  registering surface (~1013 mbar).
- **Action:**
  1. Pressurise the depth chamber (or inject a high `chan_atmos_pressure`
     via the test rig) to simulate descent past 2 m → ambient > 1200 mbar.
  2. From the host: send `SID 0x10 subfunction 0x02` (enter programming
     session).
- **Pass:**
  - DUT replies NRC 0x22 — programming session denied.
  - Surface, pressure back to ~1013 mbar.
  - Retry session control: positive response, session enters
    programming.
  - Confirms the safety property "no flash operations underwater" holds
    across the depth threshold.

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
