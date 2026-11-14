"""Keep handset page rows separate from the integration client's flat indices."""

import pytest

import uds
from test_uds import _expect_rdbi_response, _send_rdbi

pytestmark = pytest.mark.rt_ratio(100)

MENU_COUNT = 0x9100
MENU_INFO = 0x9110
MENU_VALUE = 0x9130
MENU_SAVE = 0x9350
PROPERTIES_PER_PAGE = 4


def _read(can_bus, did):
    can_bus.flush_rx()
    _send_rdbi(can_bus, did)
    return _expect_rdbi_response(can_bus, did)


def _select(can_bus, page):
    # Match the four-byte selector save captured from the OEM handset.
    can_bus.flush_rx()
    uds.send_wdbi(can_bus, MENU_SAVE, page.to_bytes(4, "big"))
    uds.expect_wdbi_positive(can_bus, MENU_SAVE)
    value = _read(can_bus, MENU_VALUE)
    assert len(value) == 16
    assert int.from_bytes(value[8:], "big") == page


@pytest.mark.parametrize("page", [0, 1, 2])
def test_handset_page_rows_match_independent_flat_settings(dut, page):
    can_bus, _shim = dut
    count = _read(can_bus, uds.DID_SETTING_COUNT)
    assert count == bytes([12])
    before = [_read(can_bus, uds.DID_SETTING_VALUE_BASE + i)
              for i in range(count[0])]
    try:
        _select(can_bus, page)
        assert _read(can_bus, MENU_COUNT) == bytes([PROPERTIES_PER_PAGE + 1])
        selector = _read(can_bus, MENU_INFO)
        assert selector[:9].rstrip(b"\0") == b"Page"
        for row in range(1, PROPERTIES_PER_PAGE + 1):
            flat_index = page * PROPERTIES_PER_PAGE + row - 1
            assert _read(can_bus, MENU_INFO + row) == _read(
                can_bus, uds.DID_SETTING_INFO_BASE + flat_index)
            assert _read(can_bus, MENU_VALUE + row) == before[flat_index]
        # Changing pages cannot alter the flat diagnostic contract or values.
        assert [_read(can_bus, uds.DID_SETTING_VALUE_BASE + i)
                for i in range(count[0])] == before
    finally:
        _select(can_bus, 0)
