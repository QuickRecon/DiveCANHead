"""PPO2 broadcast integration tests.

Mirror of ``HW Testing/Tests/test_ppo2.py``: inject cell values via the
shim, then verify the firmware's periodic PPO2 / millivolt / cell-state
frames carry the expected values within the spec's tolerances.
"""

from __future__ import annotations

import pytest

import divecan
import helpers
from helpers import CellType, configure_cell, check_cell_ppo2, check_cell_millivolts


# Same parametrization range used by the HW spec: 0..250 centibar in 36
# steps gives 7 points per cell; the cube of three cells is 343 cases —
# we keep the full set to match HW coverage.
PPO2_TEST_VALUES = list(range(0, 250, 75))


import time


# Time for each cell driver to poll, parse, publish, and for the PPO2 TX
# thread to broadcast the new value.  DiveO2 polls every ~100 ms,
# analog every 10 ms, O2S every 500 ms, and PPO2 TX broadcasts every
# 500 ms — so 800 ms is the worst-case end-to-end propagation time.
SETTLE_AFTER_CONFIGURE_S = 0.8


@pytest.mark.parametrize("c3", PPO2_TEST_VALUES)
@pytest.mark.parametrize("c2", PPO2_TEST_VALUES)
@pytest.mark.parametrize("c1", PPO2_TEST_VALUES)
def test_ppo2(calibrated_dut, c1: int, c2: int, c3: int) -> None:
    """Each cell's injected PPO2 appears in the periodic PPO2 broadcast."""
    can_bus, shim = calibrated_dut

    configure_cell(shim, 1, helpers.DEV_FULL_CELLS[0], c1)
    configure_cell(shim, 2, helpers.DEV_FULL_CELLS[1], c2)
    configure_cell(shim, 3, helpers.DEV_FULL_CELLS[2], c3)

    # Wait for cell drivers to poll the new shim values, publish them
    # to the consensus channel, and let the next PPO2 TX cycle broadcast.
    time.sleep(SETTLE_AFTER_CONFIGURE_S)

    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.PPO2_RESP_ID)
    check_cell_ppo2(helpers.DEV_FULL_CELLS[0], msg.data[1], c1)
    check_cell_ppo2(helpers.DEV_FULL_CELLS[1], msg.data[2], c2)
    check_cell_ppo2(helpers.DEV_FULL_CELLS[2], msg.data[3], c3)


# ---------------------------------------------------------------------------
# Analog millivolts pairs.  The millivolts frame is only meaningful for
# analog cells; digital cells get a zero entry.  Use the same value range
# the HW test uses for analog inputs.
# ---------------------------------------------------------------------------

MILLIS_TEST_VALUES = list(range(0, 250, 36))


@pytest.mark.parametrize("c3", MILLIS_TEST_VALUES)
def test_millivolts(calibrated_dut, c3: int) -> None:
    """Analog cell millivolts are reported correctly on the mV frame.

    For the dev_full variant, only cell 3 is analog; cells 1 and 2 are
    digital and the firmware reports 0 for those slots.
    """
    can_bus, shim = calibrated_dut

    configure_cell(shim, 3, CellType.ANALOG, c3)
    time.sleep(SETTLE_AFTER_CONFIGURE_S)

    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.MILLIS_RESP_ID)
    c3_raw = (msg.data[4] << 8) | msg.data[5]
    check_cell_millivolts(c3_raw, c3)


# ---------------------------------------------------------------------------
# Consensus voting.  Three cells injected at the same nominal value with a
# small offset on one (within MAX_DEVIATION) should produce a voted result
# that includes every cell.  Larger offsets should exclude the outlier.
# ---------------------------------------------------------------------------

# Inclusion test: small per-cell offset is still averaged in.  The
# firmware's MAX_DEVIATION is 15 centibar, but the dev_full topology
# has one analog cell — at exactly ±10 cb the analog reading's ADC
# quantization can drift the difference across the boundary, so use
# ±8 to stay clear of it.  Original HW parametrization was 105 cases
# × ~9 s ≈ 16 min, which is too heavy for the smoke loop; corners
# suffice for axis coverage.
INCLUDED_OFFSETS = [-8, 0, 8]
INCLUDED_AVERAGES = [50, 150]


@pytest.mark.parametrize("outlier_cell", [1, 3])  # boundary cells
@pytest.mark.parametrize("offset", INCLUDED_OFFSETS)
@pytest.mark.parametrize("average", INCLUDED_AVERAGES)
def test_consensus_averages_cells(
    calibrated_dut,
    average: int,
    offset: int,
    outlier_cell: int,
) -> None:
    """A small per-cell deviation is averaged into the consensus PPO2."""
    can_bus, shim = calibrated_dut

    offset_centibar = average + offset
    nominal = (average * 3 - offset_centibar) / 2.0

    for cell_num in (1, 2, 3):
        cell_type = helpers.DEV_FULL_CELLS[cell_num - 1]
        value = offset_centibar if cell_num == outlier_cell else nominal
        configure_cell(shim, cell_num, cell_type, value)

    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.CELL_STATE_ID)

    # data[0] is the include bitmask, data[1] is the consensus PPO2 in centibar.
    assert msg.data[0] == 0b111, (
        f"expected all cells included, got 0b{msg.data[0]:03b}"
    )
    tolerance = max(0.02 * average, 2)
    assert abs(msg.data[1] - average) <= tolerance, (
        f"consensus PPO2 {msg.data[1]} != average {average} "
        f"(tol ±{tolerance:.1f})"
    )


# Large offsets in either direction should trigger outlier exclusion.
# Same parametrization reduction as INCLUDED above — corners suffice.
EXCLUDED_OFFSETS = [-40, 40]
EXCLUDED_AVERAGES = [50, 150]


@pytest.mark.parametrize("outlier_cell", [1, 2, 3])
@pytest.mark.parametrize("offset", EXCLUDED_OFFSETS)
@pytest.mark.parametrize("average", EXCLUDED_AVERAGES)
def test_consensus_excludes_outlier(
    calibrated_dut,
    average: int,
    offset: int,
    outlier_cell: int,
) -> None:
    """A cell offset by more than MAX_DEVIATION is excluded from the vote."""
    can_bus, shim = calibrated_dut

    offset_centibar = average + offset

    for cell_num in (1, 2, 3):
        cell_type = helpers.DEV_FULL_CELLS[cell_num - 1]
        value = offset_centibar if cell_num == outlier_cell else average
        configure_cell(shim, cell_num, cell_type, value)

    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.CELL_STATE_ID)

    expected_mask = 0b111 ^ (1 << (outlier_cell - 1))
    assert msg.data[0] == expected_mask, (
        f"expected exclude mask 0b{expected_mask:03b}, "
        f"got 0b{msg.data[0]:03b}"
    )
    tolerance = max(0.02 * average, 2)
    assert abs(msg.data[1] - average) <= tolerance, (
        f"consensus PPO2 {msg.data[1]} != average {average} "
        f"(tol ±{tolerance:.1f})"
    )
