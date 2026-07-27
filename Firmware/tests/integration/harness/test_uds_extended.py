"""Coverage of the core UDS identification and command DIDs.

These are wire-level tests: they exercise ISO-TP framing, the central UDS
dispatcher, the individual DID handlers, and their observable NRCs.
Destructive flash erase commands remain in isolated ztests.
"""

from __future__ import annotations

import pytest

import divecan
import uds as uds_helpers
from conftest import (
    relaunch_native_sim_firmware,
    stop_native_sim_firmware,
)
from sim_shim import SharedMemShim

pytestmark = pytest.mark.rt_ratio(100)

from test_uds import (
    HOST_ID,
    UDS_NEGATIVE_RESPONSE_SID,
    UDS_SID_WRITE_DATA_BY_ID,
    _expect_rdbi_response,
    _menu_response_id,
    _reassemble_isotp,
    _send_raw_payload,
    _send_rdbi,
    _send_wdbi,
)
from test_uds_did_ota import _enter_programming_session


DID_FIRMWARE_VERSION = 0xF000
DID_HARDWARE_VERSION = 0xF001
DID_VARIANT_NAME = 0xF002
DID_SERIAL_NUMBER = 0xF003
DID_CALIBRATION_TRIGGER = 0xF241
DID_SOLENOID_OVERRIDE = 0xF242
DID_ERROR_HISTOGRAM = 0xF260
DID_ERROR_HISTOGRAM_CLEAR = 0xF261
DID_PPO2_MODE_LABEL_BASE = 0x9160

NRC_SERVICE_NOT_SUPPORTED = 0x11
NRC_INCORRECT_MSG_LEN = 0x13
NRC_CONDITIONS_NOT_CORRECT = 0x22
NRC_REQUEST_OUT_OF_RANGE = 0x31
NRC_SERVICE_NOT_IN_SESSION = 0x7F


def _expect_negative(can_bus, request_sid: int, expected_nrc: int) -> None:
    payload = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID),
    )
    assert payload == bytes(
        [0x00, UDS_NEGATIVE_RESPONSE_SID, request_sid, expected_nrc]
    ), payload.hex()


def _expect_wdbi_positive(can_bus, expected_did: int) -> None:
    payload = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID),
    )
    assert payload == bytes(
        [0x00, 0x6E, (expected_did >> 8) & 0xFF, expected_did & 0xFF]
    ), payload.hex()


@pytest.mark.parametrize(
    ("did", "min_length", "max_length"),
    [
        (DID_FIRMWARE_VERSION, 1, 10),
        (DID_HARDWARE_VERSION, 1, 1),
        (DID_VARIANT_NAME, 1, 31),
        (DID_SERIAL_NUMBER, 1, 16),
    ],
)
def test_identification_dids(dut, did: int, min_length: int,
                             max_length: int) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, did)
    payload = _expect_rdbi_response(can_bus, did)

    assert min_length <= len(payload) <= max_length
    if did == DID_HARDWARE_VERSION:
        assert payload == b"\x00"


@pytest.mark.parametrize(
    ("option", "expected"),
    [(0, b"Off"), (1, b"PID"), (2, b"MK15")],
)
def test_ppo2_mode_option_labels_are_fixed_width(
    dut, option: int, expected: bytes
) -> None:
    can_bus, _shim = dut
    did = DID_PPO2_MODE_LABEL_BASE + option

    can_bus.flush_rx()
    _send_rdbi(can_bus, did)
    payload = _expect_rdbi_response(can_bus, did)

    assert len(payload) == 9
    assert payload.rstrip(b" ") == expected


def test_invalid_option_label_returns_request_out_of_range(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_PPO2_MODE_LABEL_BASE + 0x0F)
    _expect_negative(can_bus, 0x22, NRC_REQUEST_OUT_OF_RANGE)


def test_calibration_trigger_rejects_wrong_length(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_CALIBRATION_TRIGGER, b"")
    _expect_negative(can_bus, UDS_SID_WRITE_DATA_BY_ID,
                     NRC_INCORRECT_MSG_LEN)


def test_calibration_trigger_rejects_invalid_fo2(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_CALIBRATION_TRIGGER, b"\x65")
    _expect_negative(can_bus, UDS_SID_WRITE_DATA_BY_ID,
                     NRC_REQUEST_OUT_OF_RANGE)


def test_calibration_trigger_accepts_valid_fo2(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_CALIBRATION_TRIGGER, b"\x15")
    _expect_wdbi_positive(can_bus, DID_CALIBRATION_TRIGGER)


def test_solenoid_override_rejects_wrong_length(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_SOLENOID_OVERRIDE, b"\x00")
    _expect_negative(can_bus, UDS_SID_WRITE_DATA_BY_ID,
                     NRC_INCORRECT_MSG_LEN)


def test_solenoid_override_requires_programming_session(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_SOLENOID_OVERRIDE, b"\x00\x5a")
    _expect_negative(can_bus, UDS_SID_WRITE_DATA_BY_ID,
                     NRC_SERVICE_NOT_IN_SESSION)


def test_solenoid_override_rejects_wrong_magic(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _enter_programming_session(can_bus)
    _send_wdbi(can_bus, DID_SOLENOID_OVERRIDE, b"\x00\x00")
    _expect_negative(can_bus, UDS_SID_WRITE_DATA_BY_ID,
                     NRC_REQUEST_OUT_OF_RANGE)


def test_solenoid_override_refuses_while_control_loop_owns_output(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _enter_programming_session(can_bus)
    _send_wdbi(can_bus, DID_SOLENOID_OVERRIDE, b"\x00\x5a")
    _expect_negative(can_bus, UDS_SID_WRITE_DATA_BY_ID,
                     NRC_CONDITIONS_NOT_CORRECT)


@pytest.mark.rt_ratio(10)
@pytest.mark.parametrize("channel", [0, 1, 2, 3])
def test_calibration_acquire_cancels_every_solenoid_channel(
    dut, firmware, channel: int
) -> None:
    """Calibration acquisition owns and clears the complete solenoid bank.

    Put control in OFF mode so the diagnostic override can energize each raw
    channel in turn, then issue a normal calibration request. DiveCAN sends
    its compatibility ACK immediately before claiming the guard, so poll for
    the ownership transition and require it to clear the bank well before the
    override's 1.5-second deadman could do so naturally.
    """
    can_bus, shim = dut
    proc = firmware
    flash_path = getattr(proc, "_divecan_flash_file", None)
    assert flash_path is not None, "firmware fixture must use isolated flash"

    uds_helpers.save_setting_value(
        can_bus,
        uds_helpers.SETTING_INDEX_PPO2_MODE,
        uds_helpers.PPO2_MODE_OFF,
    )

    shim.close()
    stop_native_sim_firmware(proc)
    proc = relaunch_native_sim_firmware(flash_path, rt_ratio=10)
    shim = SharedMemShim()

    try:
        shim.wait_ready()
        shim.set_bus_on()

        can_bus.flush_rx()
        _enter_programming_session(can_bus)
        _send_wdbi(
            can_bus,
            DID_SOLENOID_OVERRIDE,
            bytes([channel, 0x5A]),
        )
        _expect_wdbi_positive(can_bus, DID_SOLENOID_OVERRIDE)
        assert shim.get_solenoid_state()[channel], (
            f"precondition: override did not energize channel {channel}"
        )

        start_us = shim.get_uptime_us()
        can_bus.flush_rx()
        can_bus.send(divecan.build_cal_request())
        ack = can_bus.wait_for(divecan.CAL_RESP_ID)
        assert ack.data[0] == 0x05

        elapsed_us = shim.get_uptime_us() - start_us
        while any(shim.get_solenoid_state()) and elapsed_us < 1_000_000:
            elapsed_us = shim.get_uptime_us() - start_us

        assert elapsed_us < 1_000_000, (
            "calibration acquire did not clear the bank before the "
            "override deadman could expire"
        )
        assert not any(shim.get_solenoid_state()), (
            f"calibration acquire left a solenoid active after channel "
            f"{channel} override"
        )
    finally:
        shim.close()
        stop_native_sim_firmware(proc)


def test_error_histogram_read_and_clear(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_ERROR_HISTOGRAM)
    before = _expect_rdbi_response(can_bus, DID_ERROR_HISTOGRAM)
    assert len(before) > 0
    assert len(before) % 2 == 0

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_ERROR_HISTOGRAM_CLEAR, b"\x01")
    _expect_wdbi_positive(can_bus, DID_ERROR_HISTOGRAM_CLEAR)

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_ERROR_HISTOGRAM)
    after = _expect_rdbi_response(can_bus, DID_ERROR_HISTOGRAM)
    assert after == bytes(len(after))


def test_unknown_service_returns_service_not_supported(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_raw_payload(can_bus, b"\x00\x99")
    _expect_negative(can_bus, 0x99, NRC_SERVICE_NOT_SUPPORTED)


def test_short_wdbi_request_returns_incorrect_length(dut) -> None:
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_raw_payload(can_bus, b"\x00\x2e")
    _expect_negative(can_bus, UDS_SID_WRITE_DATA_BY_ID,
                     NRC_INCORRECT_MSG_LEN)
