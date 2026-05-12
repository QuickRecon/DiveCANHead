"""Pytest fixtures for the native_sim integration harness.

Layered fixtures:

* ``vcan`` (module): verifies ``vcan0`` is present, skips otherwise.
* ``firmware`` (function): launches the ``native_sim`` binary, hands back
  ``(proc, sock_path)``, and tears the process down on exit.
* ``shim`` (function): a connected :class:`SimShim` against ``firmware``.
* ``can_bus`` (function): a :class:`CanClient` bound to ``vcan0``.
* ``dut`` (function): convenience tuple ``(can_bus, shim)``.
"""

from __future__ import annotations

import os
import signal
import subprocess
import time
from pathlib import Path
from typing import Generator

import pytest

from divecan import CanClient
from sim_shim import SimShim


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

HARNESS_DIR: Path = Path(__file__).resolve().parent
# .../Firmware/tests/integration/harness -> .../Firmware
FIRMWARE_ROOT: Path = HARNESS_DIR.parents[2]
NATIVE_SIM_BIN: Path = (
    FIRMWARE_ROOT / "build-native" / "integration" / "zephyr" / "zephyr.exe"
)
SHIM_SOCK_PATH: str = "/tmp/divecan_shim.sock"

# Time to wait for the shim socket to bind once the binary launches. The
# firmware does this very early in main() but the syscall is asynchronous
# from our point of view.
SHIM_BIND_DELAY_S: float = 0.2

# Termination grace period before escalating to SIGKILL.
TERMINATE_GRACE_S: float = 1.0


# ---------------------------------------------------------------------------
# vcan availability
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def vcan() -> str:
    """Ensure ``vcan0`` is present; skip the module otherwise."""
    result = subprocess.run(
        ["ip", "link", "show", "vcan0"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        pytest.skip("vcan0 interface is not available")
    return "vcan0"


# ---------------------------------------------------------------------------
# Firmware lifecycle
# ---------------------------------------------------------------------------


def _remove_stale_socket(path: str) -> None:
    """Delete any leftover socket node at ``path``."""
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    except OSError as exc:
        raise RuntimeError(
            f"could not remove stale shim socket {path!r}: {exc}"
        ) from exc


@pytest.fixture()
def firmware() -> Generator[tuple[subprocess.Popen[bytes], str], None, None]:
    """Launch the native_sim binary, yield ``(proc, sock_path)``."""
    if not NATIVE_SIM_BIN.exists():
        pytest.skip(f"native_sim binary not found at {NATIVE_SIM_BIN}")

    _remove_stale_socket(SHIM_SOCK_PATH)

    # Pipe-backed stdout/stderr blocks the firmware once the kernel pipe
    # buffer (~64 KB) fills, because nothing in this fixture drains them
    # and Zephyr's printk path is synchronous on the writing thread.  Stream
    # to a file in /tmp instead so the firmware can log freely; the file
    # is available for post-test inspection.
    log_file = open("/tmp/divecan_firmware.log", "wb")
    proc = subprocess.Popen(
        [str(NATIVE_SIM_BIN)],
        stdout=log_file,
        stderr=subprocess.STDOUT,
        cwd=str(FIRMWARE_ROOT),
    )

    # Give the shim a moment to bind its socket before fixtures downstream
    # try to connect.
    time.sleep(SHIM_BIND_DELAY_S)

    try:
        yield proc, SHIM_SOCK_PATH
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=TERMINATE_GRACE_S)
            except subprocess.TimeoutExpired:
                proc.kill()
                try:
                    proc.wait(timeout=TERMINATE_GRACE_S)
                except subprocess.TimeoutExpired:
                    pass
        log_file.close()
        _remove_stale_socket(SHIM_SOCK_PATH)


# ---------------------------------------------------------------------------
# Shim and CAN clients
# ---------------------------------------------------------------------------


@pytest.fixture()
def shim(
    firmware: tuple[subprocess.Popen[bytes], str],
) -> Generator[SimShim, None, None]:
    """Yield a :class:`SimShim` connected to the running firmware."""
    _proc, sock_path = firmware
    client = SimShim(sock_path=sock_path)
    try:
        client.wait_ready()
        yield client
    finally:
        client.close()


@pytest.fixture()
def can_bus(vcan: str) -> Generator[CanClient, None, None]:
    """Yield a :class:`CanClient` bound to ``vcan0``."""
    client = CanClient(channel=vcan)
    try:
        yield client
    finally:
        client.close()


@pytest.fixture()
def dut(
    can_bus: CanClient,
    shim: SimShim,
    firmware: tuple[subprocess.Popen[bytes], str],
) -> tuple[CanClient, SimShim]:
    """Convenience tuple for tests that need both transports."""
    # ``firmware`` is requested for ordering only — Pytest will set it up
    # before this fixture and tear it down after.
    _ = firmware
    return can_bus, shim


@pytest.fixture()
def can_only_dut(
    can_bus: CanClient,
    firmware: tuple[subprocess.Popen[bytes], str],
) -> CanClient:
    """CAN-only fixture for tests that don't need the sensor-injection shim
    (ping, basic CAN protocol responses). Launches the firmware but skips
    the shim connection."""
    _ = firmware
    return can_bus


@pytest.fixture()
def calibrated_dut(dut: tuple[CanClient, SimShim]) -> tuple[CanClient, SimShim]:
    """Like ``dut`` but runs the calibration happy path before yielding so
    cell PPO2 broadcasts are not stuck on CELL_NEED_CAL (0xFF) bytes."""
    import helpers  # local import to avoid cycle at module-load time
    can_bus, shim = dut
    helpers.calibrate_board(can_bus, shim)
    return can_bus, shim
