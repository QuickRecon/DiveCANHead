"""UDS state/power DID reads + the factory flash-erase action DID.

Covers the read handlers that the other UDS suites skip:

  * PPO2-control snapshot DIDs 0xF210 (duty), 0xF211 (PID integral),
    0xF212 (saturation count) — all served from ``ppo2_control_get_snapshot``.
  * Power-rail voltage DIDs 0xF230 (VBus), 0xF233 (CAN bus) and the device
    current struct 0xF237.
  * A malformed variant: WriteDataByIdentifier against a read-only state DID
    must be refused.

And the destructive factory flash-erase action DID 0xF278:

  * Gating (wrong length / no session / wrong magic) — negative responses.
  * The happy path: in a programming session with the magic byte, the head
    ACKs and then reboots. On native_sim the external NOR node does not exist,
    so ``flash_mass_erase_external()`` is the ``-ENODEV`` stub — the handler
    still ACKs, records the failure, and reboots (process exit). We assert the
    ACK arrives and the firmware exits.
"""

from __future__ import annotations

import struct

import pytest

import divecan
import uds as uds_helpers

pytestmark = pytest.mark.rt_ratio(100)


# --- DIDs (mirror src/divecan/include/uds_state_did.h) ----------------------
DID_DUTY_CYCLE = 0xF210
DID_INTEGRAL_STATE = 0xF211
DID_SATURATION_COUNT = 0xF212
DID_VBUS_VOLTAGE = 0xF230
DID_CAN_VOLTAGE = 0xF233
DID_DEVICE_CURRENT = 0xF237
DID_FACTORY_FLASH_ERASE = 0xF278

UDS_SID_READ_DATA_BY_ID = 0x22
UDS_SID_WRITE_DATA_BY_ID = 0x2E
UDS_POSITIVE_RESPONSE_OFFSET = 0x40
UDS_NEGATIVE_RESPONSE_SID = 0x7F
UDS_NRC_INCORRECT_MSG_LEN = 0x13
UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31
UDS_NRC_SERVICE_NOT_IN_SESSION = 0x7F

OTA_MAGIC = 0x01


# --- Local UDS helpers (built on the stable uds.py module) ------------------


def _read_did(can_bus, did: int) -> bytes:
    """RDBI 0x22 → return the data bytes after pad + response SID + echoed DID."""
    uds_helpers.send_isotp_payload(
        can_bus,
        bytes([0x00, UDS_SID_READ_DATA_BY_ID, (did >> 8) & 0xFF, did & 0xFF]))
    payload = uds_helpers.reassemble_isotp(can_bus)
    assert payload[0] == 0x00, f"pad {payload.hex()}"
    assert payload[1] == UDS_SID_READ_DATA_BY_ID + UDS_POSITIVE_RESPONSE_OFFSET, (
        f"expected RDBI positive resp, got 0x{payload[1]:02X}: {payload.hex()}")
    resp_did = (payload[2] << 8) | payload[3]
    assert resp_did == did, f"echoed DID 0x{resp_did:04X} != 0x{did:04X}"
    return bytes(payload[4:])


def _expect_wdbi_negative(can_bus, expected_nrc: int) -> None:
    payload = uds_helpers.reassemble_isotp(can_bus)
    assert payload[0] == 0x00, f"pad {payload.hex()}"
    assert payload[1] == UDS_NEGATIVE_RESPONSE_SID, (
        f"expected NRC SID, got 0x{payload[1]:02X}: {payload.hex()}")
    assert payload[2] == UDS_SID_WRITE_DATA_BY_ID, (
        f"expected echoed WDBI SID, got 0x{payload[2]:02X}")
    assert payload[3] == expected_nrc, (
        f"expected NRC 0x{expected_nrc:02X}, got 0x{payload[3]:02X}")


def _enter_programming_session(can_bus) -> None:
    uds_helpers.send_isotp_payload(can_bus, bytes([0x00, 0x10, 0x02]))
    payload = uds_helpers.reassemble_isotp(can_bus)
    assert payload[1] == 0x50, f"session ctrl resp 0x{payload[1]:02X}"
    assert payload[2] == 0x02, f"subfunction echo 0x{payload[2]:02X}"


# --- Snapshot DID reads -----------------------------------------------------


def test_read_duty_cycle_did(dut) -> None:
    """0xF210 → float32 duty in [0.0, 1.0] (ppo2_control_get_snapshot)."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    data = _read_did(can_bus, DID_DUTY_CYCLE)
    assert len(data) == 4, f"duty payload {data.hex()}"
    duty = struct.unpack("<f", data)[0]
    assert 0.0 <= duty <= 1.0, f"duty {duty} out of range"


def test_read_integral_state_did(dut) -> None:
    """0xF211 → float32 PID integral accumulator (finite)."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    data = _read_did(can_bus, DID_INTEGRAL_STATE)
    assert len(data) == 4, f"integral payload {data.hex()}"
    integral = struct.unpack("<f", data)[0]
    assert integral == integral, "integral is NaN"  # NaN != NaN


def test_read_saturation_count_did(dut) -> None:
    """0xF212 → uint16 saturation event counter."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    data = _read_did(can_bus, DID_SATURATION_COUNT)
    assert len(data) == 2, f"saturation payload {data.hex()}"


def test_read_vbus_voltage_did(dut) -> None:
    """0xF230 → float32 VBus rail voltage."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    data = _read_did(can_bus, DID_VBUS_VOLTAGE)
    assert len(data) == 4, f"vbus payload {data.hex()}"


def test_read_can_voltage_did(dut) -> None:
    """0xF233 → float32 CAN bus voltage."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    data = _read_did(can_bus, DID_CAN_VOLTAGE)
    assert len(data) == 4, f"can voltage payload {data.hex()}"


def test_read_device_current_did(dut) -> None:
    """0xF237 → 8-byte device-current struct (int32 uA, u16 age, u8 valid, rsvd)."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    data = _read_did(can_bus, DID_DEVICE_CURRENT)
    assert len(data) == 8, f"device current payload {data.hex()}"
    _current_ua, _age_s, valid, reserved = struct.unpack("<iHBB", data)
    assert valid in (0, 1), f"valid flag {valid}"
    assert reserved == 0, f"reserved byte {reserved}"


def test_write_to_readonly_state_did_is_refused(dut) -> None:
    """A WriteDataByIdentifier against a read-only state DID (0xF210) must
    return a negative response rather than mutating anything."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    uds_helpers.send_wdbi(can_bus, DID_DUTY_CYCLE, b"\x00\x00\x00\x00")
    payload = uds_helpers.reassemble_isotp(can_bus)
    assert payload[1] == UDS_NEGATIVE_RESPONSE_SID, (
        f"write to read-only DID should NRC, got {payload.hex()}")


# --- Factory flash-erase action DID (0xF278) --------------------------------


def test_factory_flash_erase_wrong_length(dut) -> None:
    can_bus, _shim = dut
    can_bus.flush_rx()
    uds_helpers.send_wdbi(can_bus, DID_FACTORY_FLASH_ERASE, b"")
    _expect_wdbi_negative(can_bus, UDS_NRC_INCORRECT_MSG_LEN)


def test_factory_flash_erase_requires_session(dut) -> None:
    can_bus, _shim = dut
    can_bus.flush_rx()
    uds_helpers.send_wdbi(can_bus, DID_FACTORY_FLASH_ERASE, bytes([OTA_MAGIC]))
    _expect_wdbi_negative(can_bus, UDS_NRC_SERVICE_NOT_IN_SESSION)


def test_factory_flash_erase_wrong_magic(dut) -> None:
    can_bus, _shim = dut
    can_bus.flush_rx()
    _enter_programming_session(can_bus)
    uds_helpers.send_wdbi(can_bus, DID_FACTORY_FLASH_ERASE, bytes([0xFF]))
    _expect_wdbi_negative(can_bus, UDS_NRC_REQUEST_OUT_OF_RANGE)


def test_factory_flash_erase_acks_then_reboots(dut, firmware) -> None:
    """Happy path: session + magic → positive ACK, then the head reboots.

    On native_sim there is no external NOR node, so the erase itself is the
    ``-ENODEV`` stub — the handler ACKs, logs the incomplete erase, and reboots
    regardless. ``sys_reboot`` on native_sim exits the process, so we assert the
    firmware terminates shortly after the ACK.
    """
    can_bus, _shim = dut
    proc = firmware

    can_bus.flush_rx()
    _enter_programming_session(can_bus)
    uds_helpers.send_wdbi(can_bus, DID_FACTORY_FLASH_ERASE, bytes([OTA_MAGIC]))
    uds_helpers.expect_wdbi_positive(can_bus, DID_FACTORY_FLASH_ERASE)

    # The handler flushes the TX queue, runs the (stub) erase, then cold-reboots
    # — which on native_sim is a process exit. Give it generous wall headroom.
    try:
        proc.wait(timeout=15.0)
    except Exception:
        pytest.fail("firmware did not reboot/exit after factory flash erase ACK")
    assert proc.poll() is not None, "firmware still running after erase reboot"
