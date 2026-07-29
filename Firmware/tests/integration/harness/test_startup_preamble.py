"""Integration test for the boot-time startup preamble.

The firmware's ``emit_startup_preamble`` (``src/main.c``) runs once at
boot and writes a multi-line block to the LOG subsystem. Under
``native_sim`` the standard backend lands on stdout, which the
``firmware`` fixture captures to ``/tmp/divecan_firmware.log``. This
test launches a normal firmware instance and grep's that capture for
the expected lines.

What we verify:
  * The header / footer separators are present.
  * The firmware UID (``APP_BUILD_VERSION_STR``) and Zephyr version
    are emitted.
  * Compile-time topology matches ``tests/integration/integration.conf``
    — the self-contained config the integration build pins.
  * The runtime-config section is present and well-formed.

The integration topology enables the flash log with small simulator-only
partitions, so the preamble also verifies the cheap occupancy/current-boot
statistics without triggering the deliberately-lazy full-ring reader index.
"""

from __future__ import annotations

import tempfile
import time
from pathlib import Path
from typing import Generator

import pytest

from conftest import (
    launch_native_sim_firmware,
    stop_native_sim_firmware,
    _kill_stale_firmware,
)


# Module-scoped firmware fixture: every assertion is read-only against
# the boot log so one launch covers them all.
@pytest.fixture(scope="module")
def firmware() -> Generator[None, None, None]:
    """Launch native_sim, wait for boot, leave it running for the read.

    No ``vcan`` requirement — the preamble runs before any CAN traffic.
    A short wall delay after launch is enough for the preamble to be
    flushed to the capture file.
    """
    _kill_stale_firmware()
    with tempfile.TemporaryDirectory(prefix="divecan-preamble-") as tmpdir:
        flash_path = str(Path(tmpdir) / "flash.bin")
        proc = launch_native_sim_firmware(
            flash_file=flash_path,
            flash_erase=True,
        )
        try:
            # emit_startup_preamble() paces ~25 LOG_INF lines with a 50 ms
            # k_msleep each (main.c preamble_line, to let the priority-3 log
            # thread drain), so the block takes ~1.5 s of sim/wall time to
            # fully flush. Poll the capture until the footer lands (bounded).
            log_path = Path("/tmp/divecan_firmware.log")
            deadline = time.monotonic() + 8.0
            while time.monotonic() < deadline:
                if (log_path.exists() and "===== End preamble =====" in
                        log_path.read_bytes().decode("utf-8", errors="replace")):
                    break
                time.sleep(0.1)
            yield
        finally:
            stop_native_sim_firmware(proc)


@pytest.fixture(scope="module")
def boot_log(firmware) -> str:
    """Return the captured firmware stdout as one string."""
    _ = firmware  # ordering only
    log_path = Path("/tmp/divecan_firmware.log")
    assert log_path.exists(), "firmware fixture should have created the log"
    # The capture is binary-mode ('wb'); decode with replacement so a
    # stray non-UTF8 byte from picolibc can't break the test.
    return log_path.read_bytes().decode("utf-8", errors="replace")


def test_preamble_header_and_footer_present(boot_log: str) -> None:
    """Both separator lines bracket the preamble block."""
    assert "===== DiveCAN Boot Preamble =====" in boot_log
    assert "===== End preamble =====" in boot_log


def test_preamble_emits_firmware_uid(boot_log: str) -> None:
    """The git-describe output (or 'dev' fallback) must appear on a Firmware: line."""
    # We don't pin the exact value — it's whatever ``git describe``
    # produces at build time. Just confirm a non-empty payload after
    # the prefix.
    matches = [line for line in boot_log.splitlines() if "Firmware:" in line]
    assert len(matches) >= 1, "missing Firmware: line in preamble"
    # Strip the leading log prefix and check there's actually a value
    # after ``Firmware:``.
    suffix = matches[-1].split("Firmware:", 1)[1].strip()
    assert len(suffix) > 0, f"empty firmware UID: {matches[-1]!r}"


def test_preamble_emits_zephyr_and_board(boot_log: str) -> None:
    assert "Zephyr:" in boot_log
    # Board name comes from CONFIG_BOARD; integration builds use the
    # native_sim board.
    assert "Board: native_sim" in boot_log


def test_preamble_emits_integration_cells(boot_log: str) -> None:
    """integration.conf pins cells to DiveO2 / DiveO2 / Analog."""
    assert "Cells: 3" in boot_log
    assert "Cell[1]: DIVEO2" in boot_log
    assert "Cell[2]: DIVEO2" in boot_log
    assert "Cell[3]: ANALOG" in boot_log


def test_preamble_emits_power_mode_battery_then_can(boot_log: str) -> None:
    """integration.conf pins power mode to BATTERY_THEN_CAN with LI2S chemistry."""
    assert "Power mode: BATTERY_THEN_CAN" in boot_log
    assert "Battery chemistry (compile): LI2S" in boot_log


def test_preamble_emits_solenoid_channels(boot_log: str) -> None:
    """integration.conf populates all four solenoid channels (0..3)."""
    assert "Solenoids: O2_inject=0 O2_inject_2=1 O2_flush=2 dil_flush=3" in boot_log


def test_preamble_emits_has_flags(boot_log: str) -> None:
    """integration.conf has both solenoid types and both cell-class flavours."""
    assert "Has flags: o2_sol=Y flush_sol=Y digital_cell=Y analog_cell=Y" in boot_log


def test_preamble_emits_compile_defaults(boot_log: str) -> None:
    """integration.conf defaults: PID control, DIGITAL_REF cal, depth comp on."""
    assert "Compile defaults: ppo2=PID cal=DIGITAL_REF depth_comp=Y" in boot_log


def test_preamble_emits_runtime_section(boot_log: str) -> None:
    """Runtime block is present with all five lines."""
    assert "Runtime config:" in boot_log
    # Each line in the block has a known prefix; assert presence rather
    # than exact values (the runtime values are whatever NVS contained,
    # which on a fresh boot is the compile-time defaults).
    assert "  PPO2 mode:" in boot_log
    assert "  Cal mode:" in boot_log
    assert "  Depth comp:" in boot_log
    assert "  PID gains x1000:" in boot_log
    assert "  Battery type:" in boot_log


def test_preamble_runtime_ppo2_mode_is_pid(boot_log: str) -> None:
    """The integration build's compiled-in default latches PPO2 mode = PID,
    so ppo2_mode_name() must project the runtime setting to "PID"."""
    assert "  PPO2 mode: PID" in boot_log


def test_preamble_runtime_cal_mode_is_digital_ref(boot_log: str) -> None:
    """integration.conf defaults the cal method to DIGITAL_REFERENCE, so
    cal_mode_name() must render it as "DIGITAL_REF"."""
    assert "  Cal mode: DIGITAL_REF" in boot_log


def test_preamble_runtime_battery_type_is_li2s(boot_log: str) -> None:
    """integration.conf pins the LI2S battery chemistry, so battery_type_name()
    must render the runtime battery type as "LI2S"."""
    assert "  Battery type: LI2S" in boot_log


def test_preamble_runtime_pid_gains_are_integers(boot_log: str) -> None:
    """The Kp/Ki/Kd line uses the x1000 integer encoding (no floats)."""
    pid_lines = [
        line for line in boot_log.splitlines()
        if "PID gains x1000:" in line
    ]
    assert len(pid_lines) >= 1, "missing PID gains line"
    suffix = pid_lines[-1].split("PID gains x1000:", 1)[1]
    # Format: " Kp=<int> Ki=<int> Kd=<int>"
    for needle in ("Kp=", "Ki=", "Kd="):
        assert needle in suffix, f"missing {needle} in {suffix!r}"
    # No leftover literal '*float*' tokens from picolibc-without-FP.
    assert "*float*" not in suffix, f"picolibc FP regression: {suffix!r}"


def test_preamble_emits_flash_log_block(boot_log: str) -> None:
    """The simulator-backed telemetry/text FCB statistics are present."""
    assert "Flash log:" in boot_log
    assert "Telemetry:" in boot_log
    assert "/4 sectors used" in boot_log
    assert "Text:" in boot_log
    assert "/2 sectors used" in boot_log
