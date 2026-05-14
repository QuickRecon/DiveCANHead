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
from sim_shim import SharedMemShim, SimShim


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

HARNESS_DIR: Path = Path(__file__).resolve().parent
# .../Firmware/tests/integration/harness -> .../Firmware
FIRMWARE_ROOT: Path = HARNESS_DIR.parents[2]

# Allow callers to point at an alternate build directory — e.g. the
# sanitizer-instrumented build at ``build-native/integration-asan``.
# Set ``DIVECAN_FW_BIN`` in the environment to override.  Default is
# the standard integration build.
NATIVE_SIM_BIN: Path = Path(
    os.environ.get(
        "DIVECAN_FW_BIN",
        str(FIRMWARE_ROOT / "build-native" / "integration"
            / "zephyr" / "zephyr.exe"),
    )
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


def _kill_stale_firmware() -> None:
    """SIGKILL any leftover native_sim processes from a previous test run.

    When a test crashes or the SIGTERM-then-SIGKILL teardown sequence loses a
    race, a zephyr.exe child can survive across pytest sessions.  Multiple
    surviving processes on ``vcan0`` produce ghost traffic that breaks
    arbitration-id based filtering (frames from different firmware instances
    appear at the same id with different payloads).  Reap them before launch.
    """
    bin_path = str(NATIVE_SIM_BIN)
    result = subprocess.run(
        ["pgrep", "-f", bin_path],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            pid = int(line)
        except ValueError:
            continue
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    # Give the kernel a moment to release the vcan0 socket.
    time.sleep(0.05)


def launch_native_sim_firmware(append_log: bool = False,
                                rt_ratio: float | None = None,
                                flash_file: str | None = None,
                                flash_erase: bool = False,
                                ) -> subprocess.Popen[bytes]:
    """Spawn the native_sim binary and return the Popen handle.

    ``rt_ratio`` passes ``--rt-ratio=<N>`` to the native_sim runtime
    if set, which scales simulated time relative to wall time:

      * ``rt_ratio=1.0`` (or ``None``) — default, simulated time tracks
        wall time 1:1.
      * ``rt_ratio=10.0`` — simulated time runs **10× faster** than
        wall time.  Useful for tests that watch many control cycles
        (PID stability) and only care about per-iteration timing
        being self-consistent inside the firmware.
      * ``rt_ratio=0.1`` — simulated time runs **10× slower** than
        wall time.  Useful for ISO-TP multi-frame transfers that
        need extra wall-time headroom for IPC.

    External IPC (CAN, shim sockets) is always wall-time bound, so a
    very aggressive ratio can destabilise tests that depend on
    request/response within a bounded wall window — keep it ≥0.05.

    Exposed so power-cycle tests can simulate the silicon's
    WKUP-pin → POR mechanism by relaunching the firmware after it has
    sys_reboot'd (which on native_sim equates to ``posix_exit``).
    ``append_log=True`` opens ``/tmp/divecan_firmware.log`` in append
    mode so the second boot's output is preserved alongside the first.

    ``flash_file`` points the Zephyr flash simulator at a backing file
    so state persists across relaunches.  Without this, native_sim
    defaults to ``cwd/flash.bin`` which collides across parallel test
    invocations.  Set ``flash_erase=True`` to wipe it on launch (used
    for the first launch of a test that wants a clean slate).
    """
    if not NATIVE_SIM_BIN.exists():
        pytest.skip(f"native_sim binary not found at {NATIVE_SIM_BIN}")

    _remove_stale_socket(SHIM_SOCK_PATH)

    log_mode = "ab" if append_log else "wb"
    log_file = open("/tmp/divecan_firmware.log", log_mode)

    cmdline: list[str] = [str(NATIVE_SIM_BIN)]
    if rt_ratio is not None:
        cmdline.append(f"--rt-ratio={rt_ratio}")
    if flash_file is not None:
        cmdline.append(f"-flash={flash_file}")
        if flash_erase:
            cmdline.append("-flash_erase")

    proc = subprocess.Popen(
        cmdline,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        cwd=str(FIRMWARE_ROOT),
    )

    # Give the shim a moment to bind its socket before fixtures downstream
    # try to connect.
    time.sleep(SHIM_BIND_DELAY_S)

    # Attach the log file to the proc so the caller (or the firmware
    # fixture's teardown) can close it after termination.
    proc._divecan_log_file = log_file  # type: ignore[attr-defined]
    return proc


def stop_native_sim_firmware(proc: subprocess.Popen[bytes]) -> None:
    """Politely stop the firmware process and reap any siblings."""
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

    _kill_stale_firmware()

    log_file = getattr(proc, "_divecan_log_file", None)
    if log_file is not None:
        log_file.close()
    _remove_stale_socket(SHIM_SOCK_PATH)


@pytest.fixture()
def firmware(request) -> Generator[tuple[subprocess.Popen[bytes], str], None, None]:
    """Launch the native_sim binary, yield ``(proc, sock_path)``.

    A test that wants accelerated simulated time can attach an
    ``rt_ratio`` marker:

        @pytest.mark.rt_ratio(0.1)   # 10× faster than wall time
        def test_thing(...): ...

    No marker → real-time pacing (default).
    """
    marker = request.node.get_closest_marker("rt_ratio")
    rt_ratio = marker.args[0] if marker is not None else None

    _kill_stale_firmware()
    proc = launch_native_sim_firmware(rt_ratio=rt_ratio)

    try:
        yield proc, SHIM_SOCK_PATH
    finally:
        stop_native_sim_firmware(proc)


# ---------------------------------------------------------------------------
# Shim and CAN clients
# ---------------------------------------------------------------------------


@pytest.fixture()
def shim(
    firmware: tuple[subprocess.Popen[bytes], str],
) -> Generator[SharedMemShim, None, None]:
    """Yield a :class:`SharedMemShim` connected to the running firmware."""
    _proc, _sock_path = firmware
    client = SharedMemShim()
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
def firmware_with_flash(
    request, tmp_path,
) -> Generator[tuple[subprocess.Popen[bytes], str, str], None, None]:
    """Launch firmware with a file-backed flash simulator.

    Yields ``(proc, sock_path, flash_path)``.  The flash file lives in
    pytest's per-test ``tmp_path``, so each test gets an isolated
    backing.  The file is created empty (flash_erase=True on first
    launch), letting the firmware boot from an unwritten flash sim
    that the integration build's NVS subsystem then populates.

    Tests that want to relaunch after a sys_reboot (e.g. to inspect
    the post-activate flash state) can call
    ``relaunch_native_sim_firmware(flash_path)`` and the flash content
    is preserved.
    """
    marker = request.node.get_closest_marker("rt_ratio")
    rt_ratio = marker.args[0] if marker is not None else None

    flash_path = str(tmp_path / "flash.bin")

    _kill_stale_firmware()
    proc = launch_native_sim_firmware(rt_ratio=rt_ratio,
                                       flash_file=flash_path,
                                       flash_erase=True)

    try:
        yield proc, SHIM_SOCK_PATH, flash_path
    finally:
        stop_native_sim_firmware(proc)


def relaunch_native_sim_firmware(flash_path: str,
                                  rt_ratio: float | None = None,
                                  ) -> subprocess.Popen[bytes]:
    """Re-launch the firmware after a sys_reboot, preserving flash state.

    Used by OTA tests to simulate the post-activate cycle: the process
    exits via sys_reboot, the harness sees the exit, and a second
    invocation against the same flash file reads back what the OTA
    pipeline left behind (slot1 contents, MCUBoot trailer markers).
    """
    _kill_stale_firmware()
    return launch_native_sim_firmware(append_log=True,
                                       rt_ratio=rt_ratio,
                                       flash_file=flash_path,
                                       flash_erase=False)


@pytest.fixture()
def calibrated_dut(dut: tuple[CanClient, SimShim]) -> tuple[CanClient, SimShim]:
    """Like ``dut`` but runs the calibration happy path before yielding so
    cell PPO2 broadcasts are not stuck on CELL_NEED_CAL (0xFF) bytes."""
    import helpers  # local import to avoid cycle at module-load time
    can_bus, shim = dut
    helpers.calibrate_board(can_bus, shim)
    return can_bus, shim
