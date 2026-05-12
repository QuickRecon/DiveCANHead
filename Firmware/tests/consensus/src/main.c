/**
 * @file main.c
 * @brief Consensus algorithm unit tests
 *
 * Pure host build — no Zephyr threads or hardware. Tests consensus_calculate()
 * and consensus_confidence() in oxygen_cell_math.c, which implement the
 * 3-cell voting algorithm (outlier exclusion, staleness, status filtering).
 * Uses a permutation helper to verify that the algorithm is insensitive to
 * cell ordering in the input array.
 */

#include <zephyr/ztest.h>
#include "oxygen_cell_math.h"

/* Staleness timeout used across all tests: 10 seconds at 1kHz tick rate */
#define STALENESS_TICKS 10000LL
#define NOW_TICKS       0LL

/**
 * @brief Run a consensus test across all 6 orderings of 3 cells.
 *
 * Verifies that consensus_calculate() produces the same result regardless of
 * which physical position each cell occupies in the input array.  Expected
 * arrays are indexed by *logical* cell identity (0=c1, 1=c2, 2=c3); the
 * helper remaps them to match each permutation before asserting.
 */
static void check_consensus_permutations(
    const ConsensusMsg_t *expected,
    const OxygenCellMsg_t *c1,
    const OxygenCellMsg_t *c2,
    const OxygenCellMsg_t *c3)
{
    const OxygenCellMsg_t *cells_src[3] = {c1, c2, c3};

    static const uint8_t perms[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
    };

    for (int p = 0; p < 6; p++) {
        uint8_t p0 = perms[p][0];
        uint8_t p1 = perms[p][1];
        uint8_t p2 = perms[p][2];

        OxygenCellMsg_t input[3] = {
            *cells_src[p0],
            *cells_src[p1],
            *cells_src[p2],
        };

        ConsensusMsg_t result = consensus_calculate(
            input, 3, NOW_TICKS, STALENESS_TICKS);

        zassert_equal(result.status_array[0],
                  expected->status_array[p0],
                  "perm %d: status[0]", p);
        zassert_equal(result.status_array[1],
                  expected->status_array[p1],
                  "perm %d: status[1]", p);
        zassert_equal(result.status_array[2],
                  expected->status_array[p2],
                  "perm %d: status[2]", p);

        zassert_equal(result.ppo2_array[0],
                  expected->ppo2_array[p0],
                  "perm %d: ppo2[0]", p);
        zassert_equal(result.ppo2_array[1],
                  expected->ppo2_array[p1],
                  "perm %d: ppo2[1]", p);
        zassert_equal(result.ppo2_array[2],
                  expected->ppo2_array[p2],
                  "perm %d: ppo2[2]", p);

        zassert_equal(result.milli_array[0],
                  expected->milli_array[p0],
                  "perm %d: milli[0]", p);
        zassert_equal(result.milli_array[1],
                  expected->milli_array[p1],
                  "perm %d: milli[1]", p);
        zassert_equal(result.milli_array[2],
                  expected->milli_array[p2],
                  "perm %d: milli[2]", p);

        zassert_equal(result.include_array[0],
                  expected->include_array[p0],
                  "perm %d: include[0]", p);
        zassert_equal(result.include_array[1],
                  expected->include_array[p1],
                  "perm %d: include[1]", p);
        zassert_equal(result.include_array[2],
                  expected->include_array[p2],
                  "perm %d: include[2]", p);

        zassert_equal(result.consensus_ppo2,
                  expected->consensus_ppo2,
                  "perm %d: consensus", p);
    }
}

/** @brief Construct an OxygenCellMsg_t with all fields set for use in tests. */
static OxygenCellMsg_t make_cell(uint8_t num, PPO2_t ppo2, double prec,
                 Millivolts_t mv, CellStatus_t status,
                 int64_t ts)
{
    return (OxygenCellMsg_t){
        .cell_number = num,
        .ppo2 = ppo2,
        .precision_ppo2 = prec,
        .millivolts = mv,
        .status = status,
        .timestamp_ticks = ts,
    };
}

/** @brief Suite: three-cell consensus voting and averaging algorithm. */
ZTEST_SUITE(consensus, NULL, NULL, NULL, NULL, NULL);

/** @brief Three agreeing cells are all included and their PPO2 is averaged. */
ZTEST(consensus, test_averages_cells)
{
    OxygenCellMsg_t c1 = make_cell(0, 110, 1.1, 12, CELL_OK, 0);
    OxygenCellMsg_t c2 = make_cell(1, 115, 1.15, 13, CELL_OK, 0);
    OxygenCellMsg_t c3 = make_cell(2, 100, 1.0, 14, CELL_OK, 0);

    ConsensusMsg_t expected = {
        .consensus_ppo2 = 108,
        .status_array = {CELL_OK, CELL_OK, CELL_OK},
        .ppo2_array = {110, 115, 100},
        .milli_array = {12, 13, 14},
        .include_array = {true, true, true},
    };

    check_consensus_permutations(&expected, &c1, &c2, &c3);
}

/** @brief A cell reading significantly higher than the other two is excluded from the consensus. */
ZTEST(consensus, test_excludes_high)
{
    OxygenCellMsg_t c1 = make_cell(0, 110, 1.1, 0, CELL_OK, 0);
    OxygenCellMsg_t c2 = make_cell(1, 130, 1.3, 0, CELL_OK, 0);
    OxygenCellMsg_t c3 = make_cell(2, 100, 1.0, 0, CELL_OK, 0);

    ConsensusMsg_t expected = {
        .consensus_ppo2 = 105,
        .status_array = {CELL_OK, CELL_OK, CELL_OK},
        .ppo2_array = {110, 130, 100},
        .milli_array = {0, 0, 0},
        .include_array = {true, false, true},
    };

    check_consensus_permutations(&expected, &c1, &c2, &c3);
}

/** @brief A cell reading very far above the pair is also excluded (large spread). */
ZTEST(consensus, test_excludes_very_high)
{
    OxygenCellMsg_t c1 = make_cell(0, 50, 0.5, 0, CELL_OK, 0);
    OxygenCellMsg_t c2 = make_cell(1, 130, 1.3, 0, CELL_OK, 0);
    OxygenCellMsg_t c3 = make_cell(2, 60, 0.6, 0, CELL_OK, 0);

    ConsensusMsg_t expected = {
        .consensus_ppo2 = 55,
        .status_array = {CELL_OK, CELL_OK, CELL_OK},
        .ppo2_array = {50, 130, 60},
        .milli_array = {0, 0, 0},
        .include_array = {true, false, true},
    };

    check_consensus_permutations(&expected, &c1, &c2, &c3);
}

/** @brief A cell reading significantly lower than the other two is excluded. */
ZTEST(consensus, test_excludes_low)
{
    OxygenCellMsg_t c1 = make_cell(0, 120, 1.2, 0, CELL_OK, 0);
    OxygenCellMsg_t c2 = make_cell(1, 130, 1.3, 0, CELL_OK, 0);
    OxygenCellMsg_t c3 = make_cell(2, 100, 1.0, 0, CELL_OK, 0);

    ConsensusMsg_t expected = {
        .consensus_ppo2 = 125,
        .status_array = {CELL_OK, CELL_OK, CELL_OK},
        .ppo2_array = {120, 130, 100},
        .milli_array = {0, 0, 0},
        .include_array = {true, true, false},
    };

    check_consensus_permutations(&expected, &c1, &c2, &c3);
}

/** @brief A cell reading very far below the pair is also excluded (large spread). */
ZTEST(consensus, test_excludes_very_low)
{
    OxygenCellMsg_t c1 = make_cell(0, 120, 1.2, 0, CELL_OK, 0);
    OxygenCellMsg_t c2 = make_cell(1, 130, 1.3, 0, CELL_OK, 0);
    OxygenCellMsg_t c3 = make_cell(2, 50, 0.5, 0, CELL_OK, 0);

    ConsensusMsg_t expected = {
        .consensus_ppo2 = 125,
        .status_array = {CELL_OK, CELL_OK, CELL_OK},
        .ppo2_array = {120, 130, 50},
        .milli_array = {0, 0, 0},
        .include_array = {true, true, false},
    };

    check_consensus_permutations(&expected, &c1, &c2, &c3);
}

/**
 * @brief A cell whose timestamp is older than STALENESS_TICKS is excluded.
 *
 * Iterates over each cell position to confirm that staleness detection is
 * position-independent.
 */
ZTEST(consensus, test_excludes_timed_out_cell)
{
    uint8_t expected_consensus[] = {107, 109, 112};

    for (int i = 0; i < 3; i++) {
        int64_t stale = -15000LL; /* 15s in the past relative to now=0 */

        OxygenCellMsg_t c1 = make_cell(0, 115, 1.15f, 0, CELL_OK,
                           (i == 0) ? stale : 0);
        OxygenCellMsg_t c2 = make_cell(1, 110, 1.1f, 0, CELL_OK,
                           (i == 1) ? stale : 0);
        OxygenCellMsg_t c3 = make_cell(2, 105, 1.05f, 0, CELL_OK,
                           (i == 2) ? stale : 0);

        ConsensusMsg_t expected = {
            .consensus_ppo2 = expected_consensus[i],
            .status_array = {CELL_OK, CELL_OK, CELL_OK},
            .ppo2_array = {115, 110, 105},
            .milli_array = {0, 0, 0},
            .include_array = {
                (i != 0), (i != 1), (i != 2),
            },
        };

        check_consensus_permutations(&expected, &c1, &c2, &c3);
    }
}

/**
 * @brief A cell with CELL_FAIL status is excluded from voting.
 *
 * Iterates over each cell position to confirm that status filtering is
 * position-independent.
 */
ZTEST(consensus, test_excludes_failed_cell)
{
    uint8_t expected_consensus[] = {107, 109, 112};

    for (int i = 0; i < 3; i++) {
        OxygenCellMsg_t c1 = make_cell(0, 115, 1.15f, 0,
                           (i == 0) ? CELL_FAIL : CELL_OK, 0);
        OxygenCellMsg_t c2 = make_cell(1, 110, 1.1f, 0,
                           (i == 1) ? CELL_FAIL : CELL_OK, 0);
        OxygenCellMsg_t c3 = make_cell(2, 105, 1.05f, 0,
                           (i == 2) ? CELL_FAIL : CELL_OK, 0);

        ConsensusMsg_t expected = {
            .consensus_ppo2 = expected_consensus[i],
            .status_array = {
                (i == 0) ? CELL_FAIL : CELL_OK,
                (i == 1) ? CELL_FAIL : CELL_OK,
                (i == 2) ? CELL_FAIL : CELL_OK,
            },
            .ppo2_array = {115, 110, 105},
            .milli_array = {0, 0, 0},
            .include_array = {(i != 0), (i != 1), (i != 2)},
        };

        check_consensus_permutations(&expected, &c1, &c2, &c3);
    }
}

/** @brief A cell with CELL_NEED_CAL status is excluded from voting. */
ZTEST(consensus, test_excludes_cal_cell)
{
    uint8_t expected_consensus[] = {107, 109, 112};

    for (int i = 0; i < 3; i++) {
        OxygenCellMsg_t c1 = make_cell(0, 115, 1.15f, 0,
                           (i == 0) ? CELL_NEED_CAL : CELL_OK, 0);
        OxygenCellMsg_t c2 = make_cell(1, 110, 1.1f, 0,
                           (i == 1) ? CELL_NEED_CAL : CELL_OK, 0);
        OxygenCellMsg_t c3 = make_cell(2, 105, 1.05f, 0,
                           (i == 2) ? CELL_NEED_CAL : CELL_OK, 0);

        ConsensusMsg_t expected = {
            .consensus_ppo2 = expected_consensus[i],
            .status_array = {
                (i == 0) ? CELL_NEED_CAL : CELL_OK,
                (i == 1) ? CELL_NEED_CAL : CELL_OK,
                (i == 2) ? CELL_NEED_CAL : CELL_OK,
            },
            .ppo2_array = {115, 110, 105},
            .milli_array = {0, 0, 0},
            .include_array = {(i != 0), (i != 1), (i != 2)},
        };

        check_consensus_permutations(&expected, &c1, &c2, &c3);
    }
}

/** @brief A cell with CELL_DEGRADED status is excluded from voting. */
ZTEST(consensus, test_excludes_degraded_cell)
{
    uint8_t expected_consensus[] = {107, 109, 112};

    for (int i = 0; i < 3; i++) {
        OxygenCellMsg_t c1 = make_cell(0, 115, 1.15f, 0,
                           (i == 0) ? CELL_DEGRADED : CELL_OK, 0);
        OxygenCellMsg_t c2 = make_cell(1, 110, 1.1f, 0,
                           (i == 1) ? CELL_DEGRADED : CELL_OK, 0);
        OxygenCellMsg_t c3 = make_cell(2, 105, 1.05f, 0,
                           (i == 2) ? CELL_DEGRADED : CELL_OK, 0);

        ConsensusMsg_t expected = {
            .consensus_ppo2 = expected_consensus[i],
            .status_array = {
                (i == 0) ? CELL_DEGRADED : CELL_OK,
                (i == 1) ? CELL_DEGRADED : CELL_OK,
                (i == 2) ? CELL_DEGRADED : CELL_OK,
            },
            .ppo2_array = {115, 110, 105},
            .milli_array = {0, 0, 0},
            .include_array = {(i != 0), (i != 1), (i != 2)},
        };

        check_consensus_permutations(&expected, &c1, &c2, &c3);
    }
}

/**
 * @brief Two failed cells leave one good cell: its value is used as consensus but voted out.
 *
 * A single surviving cell cannot form a majority vote, so include_array is all
 * false even though the consensus_ppo2 reflects that cell's reading.
 */
ZTEST(consensus, test_dual_cell_failure)
{
    uint8_t expected_consensus[] = {120, 110, 100};

    for (int i = 0; i < 3; i++) {
        OxygenCellMsg_t c1 = make_cell(0, 120, 1.2, 0,
                           (i == 0) ? CELL_OK : CELL_FAIL, 0);
        OxygenCellMsg_t c2 = make_cell(1, 110, 1.1, 0,
                           (i == 1) ? CELL_OK : CELL_FAIL, 0);
        OxygenCellMsg_t c3 = make_cell(2, 100, 1.0, 0,
                           (i == 2) ? CELL_OK : CELL_FAIL, 0);

        ConsensusMsg_t expected = {
            .consensus_ppo2 = expected_consensus[i],
            .status_array = {
                (i == 0) ? CELL_OK : CELL_FAIL,
                (i == 1) ? CELL_OK : CELL_FAIL,
                (i == 2) ? CELL_OK : CELL_FAIL,
            },
            .ppo2_array = {120, 110, 100},
            .milli_array = {0, 0, 0},
            /* Single cell used for value but voted out */
            .include_array = {false, false, false},
        };

        check_consensus_permutations(&expected, &c1, &c2, &c3);
    }
}

/** @brief Single surviving cell with a diverged value: still used for consensus, voted out. */
ZTEST(consensus, test_diverged_dual_cell_failure)
{
    uint8_t expected_consensus[] = {200, 100, 20};

    for (int i = 0; i < 3; i++) {
        OxygenCellMsg_t c1 = make_cell(0, 200, 2.0, 0,
                           (i == 0) ? CELL_OK : CELL_FAIL, 0);
        OxygenCellMsg_t c2 = make_cell(1, 100, 1.0, 0,
                           (i == 1) ? CELL_OK : CELL_FAIL, 0);
        OxygenCellMsg_t c3 = make_cell(2, 20, 0.2, 0,
                           (i == 2) ? CELL_OK : CELL_FAIL, 0);

        ConsensusMsg_t expected = {
            .consensus_ppo2 = expected_consensus[i],
            .status_array = {
                (i == 0) ? CELL_OK : CELL_FAIL,
                (i == 1) ? CELL_OK : CELL_FAIL,
                (i == 2) ? CELL_OK : CELL_FAIL,
            },
            .ppo2_array = {200, 100, 20},
            .milli_array = {0, 0, 0},
            .include_array = {false, false, false},
        };

        check_consensus_permutations(&expected, &c1, &c2, &c3);
    }
}

/** @brief All three cells failed: consensus is PPO2_FAIL (0xFF) and all are excluded. */
ZTEST(consensus, test_all_cells_excluded)
{
    OxygenCellMsg_t c1 = make_cell(0, 120, 1.2, 0, CELL_FAIL, 0);
    OxygenCellMsg_t c2 = make_cell(1, 110, 1.1, 0, CELL_FAIL, 0);
    OxygenCellMsg_t c3 = make_cell(2, 100, 1.0, 0, CELL_FAIL, 0);

    ConsensusMsg_t expected = {
        .consensus_ppo2 = PPO2_FAIL,
        .status_array = {CELL_FAIL, CELL_FAIL, CELL_FAIL},
        .ppo2_array = {120, 110, 100},
        .milli_array = {0, 0, 0},
        .include_array = {false, false, false},
    };

    check_consensus_permutations(&expected, &c1, &c2, &c3);
}

/**
 * @brief One CELL_FAIL and one zero-reading CELL_OK: the zero cell is treated as an outlier.
 *
 * The single non-failed, non-zero cell provides the consensus value but cannot
 * form a majority, so include_array is all false.
 */
ZTEST(consensus, test_fail_and_zeroed_cell)
{
    OxygenCellMsg_t c1 = make_cell(0, 25, 1.1, 12, CELL_FAIL, 0);
    OxygenCellMsg_t c2 = make_cell(1, 21, 1.15, 13, CELL_OK, 0);
    OxygenCellMsg_t c3 = make_cell(2, 0, 0.0, 0, CELL_OK, 0);

    ConsensusMsg_t expected = {
        .consensus_ppo2 = 21,
        .status_array = {CELL_FAIL, CELL_OK, CELL_OK},
        .ppo2_array = {25, 21, 0},
        .milli_array = {12, 13, 0},
        .include_array = {false, false, false},
    };

    check_consensus_permutations(&expected, &c1, &c2, &c3);
}

/**
 * @brief One CELL_FAIL and one cell reading PPO2_FAIL (0xFF): treated same as zeroed case.
 */
ZTEST(consensus, test_fail_and_fail_valued_cell)
{
    OxygenCellMsg_t c1 = make_cell(0, 25, 1.1, 12, CELL_FAIL, 0);
    OxygenCellMsg_t c2 = make_cell(1, 21, 1.15, 13, CELL_OK, 0);
    OxygenCellMsg_t c3 = make_cell(2, PPO2_FAIL, 0.0, 0, CELL_OK, 0);

    ConsensusMsg_t expected = {
        .consensus_ppo2 = 21,
        .status_array = {CELL_FAIL, CELL_OK, CELL_OK},
        .ppo2_array = {25, 21, PPO2_FAIL},
        .milli_array = {12, 13, 0},
        .include_array = {false, false, false},
    };

    check_consensus_permutations(&expected, &c1, &c2, &c3);
}

/** @brief consensus_confidence() counts the number of included (true) cells in include_array. */
ZTEST(consensus, test_confidence)
{
    struct {
        bool inc[3];
        uint8_t expected;
    } cases[] = {
        {{true, true, true}, 3},
        {{true, true, false}, 2},
        {{true, false, false}, 1},
        {{false, false, false}, 0},
    };

    for (int i = 0; i < 4; i++) {
        ConsensusMsg_t c = {
            .include_array = {
                cases[i].inc[0],
                cases[i].inc[1],
                cases[i].inc[2],
            },
        };

        zassert_equal(consensus_confidence(&c), cases[i].expected,
                  "confidence case %d", i);
    }
}

/**
 * @brief Single-cell configuration: value used for consensus, but voted out (no majority possible).
 */
ZTEST(consensus, test_single_cell)
{
    OxygenCellMsg_t cell = make_cell(0, 100, 1.0, 10, CELL_OK, 0);

    ConsensusMsg_t result = consensus_calculate(&cell, 1, NOW_TICKS,
                            STALENESS_TICKS);

    /* Single cell: value used, but voted out (no actual vote possible) */
    zassert_equal(result.consensus_ppo2, 100);
    zassert_equal(result.confidence, 0);
    zassert_false(result.include_array[0]);
}

/** @brief Two-cell configuration where both agree: both included, consensus is their average. */
ZTEST(consensus, test_two_cells_agree)
{
    OxygenCellMsg_t cells[2] = {
        make_cell(0, 100, 1.0, 10, CELL_OK, 0),
        make_cell(1, 110, 1.1, 11, CELL_OK, 0),
    };

    ConsensusMsg_t result = consensus_calculate(cells, 2, NOW_TICKS,
                            STALENESS_TICKS);

    zassert_equal(result.consensus_ppo2, 105);
    zassert_equal(result.confidence, 2);
    zassert_true(result.include_array[0]);
    zassert_true(result.include_array[1]);
}

/** @brief Two-cell configuration where both diverge: neither wins the vote, consensus = PPO2_FAIL. */
ZTEST(consensus, test_two_cells_disagree)
{
    OxygenCellMsg_t cells[2] = {
        make_cell(0, 50, 0.5, 5, CELL_OK, 0),
        make_cell(1, 130, 1.3, 13, CELL_OK, 0),
    };

    ConsensusMsg_t result = consensus_calculate(cells, 2, NOW_TICKS,
                            STALENESS_TICKS);

    /* Both voted out — consensus stays PPO2_FAIL */
    zassert_equal(result.consensus_ppo2, PPO2_FAIL);
    zassert_equal(result.confidence, 0);
}

/**
 * @brief Bug #5 regression: averaged PPO2 >254 centibar must saturate to PPO2_FAIL.
 *
 * Prevents wrapping the uint8_t result, which could silently report a safe low
 * reading in place of a dangerously high one.
 */
ZTEST(consensus, test_overflow_saturates)
{
    OxygenCellMsg_t c1 = make_cell(0, 254, 2.55, 0, CELL_OK, 0);
    OxygenCellMsg_t c2 = make_cell(1, 254, 2.55, 0, CELL_OK, 0);
    OxygenCellMsg_t c3 = make_cell(2, 254, 2.55, 0, CELL_OK, 0);

    OxygenCellMsg_t cells[3] = {c1, c2, c3};

    ConsensusMsg_t result = consensus_calculate(cells, 3, NOW_TICKS,
                            STALENESS_TICKS);

    /* 2.55 bar * 100 = 255 > MAX_VALID_PPO2(254), must saturate */
    zassert_equal(result.consensus_ppo2, PPO2_FAIL);
}
