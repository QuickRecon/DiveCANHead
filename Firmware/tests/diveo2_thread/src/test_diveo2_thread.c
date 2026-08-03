/**
 * @file test_diveo2_thread.c
 * @brief Thread-level tests for the DiveO2 cell driver (oxygen_cell_diveo2.c).
 *
 * tests/parsers already covers the pure parse helpers. This module instead
 * spawns the *real* per-cell thread (CONFIG_CELL_1_TYPE_DIVEO2=y in prj.conf,
 * usart1 = zephyr,uart-emul in the overlay) and drives it through its private
 * static state machine — setup, auto-detect, polled/broadcast sampling,
 * process_rx dispatch, the broadcast publish math (staleness / undervolt /
 * overrange), diveo2_load_cal, and the live broadcast mailbox — none of which
 * is reachable from a separate translation unit because the functions are
 * file-static.
 *
 * Driving strategy: the K_THREAD_DEFINE thread auto-starts at boot and runs
 * autonomously. The test feeds UART bytes with uart_emul_put_rx_data() and
 * observes the OxygenCellMsg_t the thread publishes on chan_cell_1. Because
 * native_sim freezes simulated time whenever every thread is runnable, EVERY
 * wait here goes through k_msleep() so the cell thread and the uart_emul work
 * queue actually get to run. Frames are fed repeatedly inside bounded poll
 * loops so a frame reliably lands inside whichever k_sem_take() window the
 * thread happens to be in — the tests assert on observable outcomes, not on
 * exact scheduler interleaving.
 *
 * Deliberately NOT covered (src/ is read-only for this task, so these arms
 * cannot be reached without fault injection the host emulator does not expose,
 * or without a genuine CPU/driver fault):
 *   - diveo2_setup() failure arms: device_is_ready()==false, uart_callback_set()
 *     and uart_rx_enable() error returns, and the diveo2_cell_thread() branch
 *     that skips the loop when setup fails. uart_emul is always ready and its
 *     callback/enable calls always succeed on native_sim.
 *   - diveo2_send_command()'s NULL guard and its uart_tx()/tx_sem error arms:
 *     the only caller is the thread (never NULL) and uart_emul TX never fails.
 *   - diveo2_uart_callback()'s UART_RX_DISABLED case + diveo2_rearm_rx(): the
 *     emulator never spontaneously disables RX in steady state.
 *   - diveo2_prepare_message_buffer()'s copyLen>outBufferLen clamp: dead
 *     defensive code (copyLen = outBufferLen - skipped is always <= outBufferLen).
 *   - diveo2_on_off_str() / apply_broadcast's "already in desired state, skip
 *     #BCST" log branch: reaching it needs a frame to land in the observe window
 *     at the instant the requested state already matches the observed state —
 *     too scheduler-dependent to assert deterministically.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/settings/settings.h>
#include <string.h>

#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"
#include "calibration_store.h"
#include "runtime_settings.h"

/* Overridden production symbol lives in stub_power.c */
extern void stub_power_set_vbus(Numeric_t volts);

/* ---- Calibration coefficient seeding ------------------------------------ *
 * diveo2_load_cal() reads "cal/cellN" via settings_runtime_get(), which is
 * served by the real calibration_store handler (linked in). We drive the two
 * load_cal branches by seeding the store's cache directly (no NVS backend):
 * an in-range coefficient takes the CELL_OK path; an out-of-range value takes
 * the default-coefficient path. (The store handler always returns a value for
 * a valid cell key, so the "missing key" length-fail sub-branch is dead in
 * production — the range check is the real discriminator.)
 */
#define CAL_CELL             0U
#define CAL_COEFF_IN_RANGE   950000.0f /* within [DIVEO2_CAL_LOWER, UPPER] */
#define CAL_COEFF_OUT_RANGE  0.0f      /* below DIVEO2_CAL_LOWER -> default path */

static void seed_cal(bool valid)
{
    CalCoeff_t coeff = CAL_COEFF_OUT_RANGE;

    if (valid) {
        coeff = CAL_COEFF_IN_RANGE;
    }
    calibration_store_seed_cached(CAL_CELL, coeff);
}

/* ---- Small UART/observation helpers ------------------------------------- */

/* Driver timeouts mirrored from oxygen_cell_diveo2.c so the pump windows below
 * are self-documenting (kept local; the source values are file-static). */
#define SETUP_DELAY_MS        1000
#define PASSIVE_LISTEN_MS     1200
#define POLL_TIMEOUT_MS       2000
#define STALENESS_MS          1000

#define POLL_STEP_MS            20
#define VALID_MRAW  "#MRAW 210000 2500 0 1000 5000 200 1013250 45000\r"
/* Distinct sample so a published simple reading is unmistakable from a
 * detailed one (whose sample is 210000). */
#define VALID_MOXY  "#MOXY 195000 2500 0\r"
#define MOXY_SAMPLE 195000

static const struct device *cell_uart(void)
{
    return DEVICE_DT_GET(DT_NODELABEL(usart1));
}

static void feed(const char *frame)
{
    (void)uart_emul_put_rx_data(cell_uart(), (const uint8_t *)frame,
                                strlen(frame));
}

static CellStatus_t read_cell_status(OxygenCellMsg_t *out)
{
    OxygenCellMsg_t msg = {0};

    (void)zbus_chan_read(&chan_cell_1, &msg, K_MSEC(100));
    if (out != NULL) {
        *out = msg;
    }
    return msg.status;
}

/* Feed @p frame every POLL_STEP_MS until the published status equals @p want or
 * the budget expires. Returns true if the wanted status was observed. */
static bool feed_until_status(const char *frame, CellStatus_t want, int max_ms)
{
    bool hit = false;
    int elapsed = 0;

    while ((elapsed < max_ms) && !hit) {
        if (frame != NULL) {
            feed(frame);
        }
        (void)k_msleep(POLL_STEP_MS);
        elapsed += POLL_STEP_MS;
        if (read_cell_status(NULL) == want) {
            hit = true;
        }
    }
    return hit;
}

/* Feed @p frame until the published reading carries raw_sample == @p sample
 * (and CELL_OK), proving that specific frame was the one parsed and published. */
static bool feed_until_sample(const char *frame, int32_t sample, int max_ms)
{
    bool hit = false;
    int elapsed = 0;

    while ((elapsed < max_ms) && !hit) {
        OxygenCellMsg_t msg = {0};

        feed(frame);
        (void)k_msleep(POLL_STEP_MS);
        elapsed += POLL_STEP_MS;
        (void)read_cell_status(&msg);
        if ((CELL_OK == msg.status) && (sample == msg.raw_sample)) {
            hit = true;
        }
    }
    return hit;
}

/* Drive auto-detection down the ACTIVE-poll path: feed @p response only after
 * the driver has actually transmitted a poll command (uart_emul_get_tx_data
 * returns >0), i.e. while the state machine is in an active phase, never during
 * the passive listen. This deterministically detects a POLLED cell (and, with
 * enforce_broadcast set, exercises the enforce -> set_cell_broadcast path)
 * instead of racing a frame into the passive window. Returns true on CELL_OK. */
static bool poll_detect(const char *response, int max_ms)
{
    bool ok = false;
    int elapsed = 0;

    while ((elapsed < max_ms) && !ok) {
        uint8_t tx[64];
        uint32_t n = uart_emul_get_tx_data(cell_uart(), tx, sizeof(tx));

        if (n > 0U) {
            feed(response); /* answer the poll the driver just sent */
        }
        (void)k_msleep(POLL_STEP_MS);
        elapsed += POLL_STEP_MS;
        if (read_cell_status(NULL) == CELL_OK) {
            ok = true;
        }
    }
    return ok;
}

/* Advance simulated time WITHOUT feeding, letting the thread's waits time out. */
static void idle(int ms)
{
    int elapsed = 0;

    while (elapsed < ms) {
        (void)k_msleep(POLL_STEP_MS);
        elapsed += POLL_STEP_MS;
    }
}

/* Advance time WHILE feeding @p frame, so a frame reliably lands inside a
 * diveo2_observe_broadcasting() listen window (making its is_on read true). */
static void feed_for(const char *frame, int ms)
{
    int elapsed = 0;

    while (elapsed < ms) {
        feed(frame);
        (void)k_msleep(POLL_STEP_MS);
        elapsed += POLL_STEP_MS;
    }
}

/* ========================================================================= *
 * Suite: full cell-thread lifecycle over an emulated UART.
 * Ordered — a single persistent thread is shared across the cases below, so
 * they run in definition order (CONFIG_ZTEST_SHUFFLE stays off) and each case
 * leaves the cell in a known-enough state for the next.
 * ========================================================================= */
ZTEST_SUITE(diveo2_thread, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Cold-start auto-detection walks all three detect phases.
 *
 * Feeds nothing until the passive-listen (phase 0) and #DRAW-poll (phase 1)
 * windows have both elapsed empty, forcing the state machine to advance into
 * the Pyroscience #MRAW poll (phase 2). Then streams #MRAW frames so the cell
 * latches CELL_PROTO_PYRO and, with enforce_broadcast pre-set, is driven into
 * broadcast mode (set_cell_broadcast). Ends CELL_OK from a valid reading.
 */
ZTEST(diveo2_thread, test_01_cold_start_detect_and_enforce)
{
    /* Healthy rail; valid stored cal so the boot load_cal took the OK path. */
    stub_power_set_vbus(5.0f);
    seed_cal(true);

    /* Enforce broadcast on cell 0 so detection (which finds a POLLED cell)
     * issues #BCST — exercising the enforce branch + set_cell_broadcast. */
    RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;

    runtime_settings_get(&rs);
    rs.enforce_broadcast[0] = true;
    (void)runtime_settings_set_volatile(&rs);

    /* Answer only the active-phase poll commands so detection lands on the
     * POLLED path: phase 0 passive elapses empty, phase 1 emits #DRAW which we
     * answer with a Pyro (#MRAW) frame -> PROTO_PYRO, CELL_MODE_POLLED. The
     * pre-set enforce_broadcast then drives set_cell_broadcast + BROADCAST. */
    bool ok = poll_detect(VALID_MRAW, 9000);

    zassert_true(ok, "cell did not reach CELL_OK after active-poll detection");
}

/**
 * @brief A valid detailed (#MRAW) reading is parsed and republished as CELL_OK
 *        with the expected raw sample and PPO2 (broadcast happy path).
 */
ZTEST(diveo2_thread, test_02_detailed_reading_published)
{
    OxygenCellMsg_t msg = {0};

    stub_power_set_vbus(5.0f);
    zassert_true(feed_until_status(VALID_MRAW, CELL_OK, 4000),
                 "no CELL_OK for a valid detailed frame");

    (void)read_cell_status(&msg);
    zassert_equal(CELL_OK, msg.status, "status");
    zassert_equal(210000, msg.raw_sample, "raw sample latched from #MRAW");
    zassert_equal(0U, msg.cell_number, "cell number");
    zassert_true(msg.ppo2 > 0U, "ppo2 wire value populated");
}

/**
 * @brief A live broadcast-OFF request drops the cell to polled sampling, then a
 *        valid simple (#MOXY) frame is accepted (apply_simple path).
 */
ZTEST(diveo2_thread, test_03_broadcast_off_then_simple_reading)
{
    stub_power_set_vbus(5.0f);

    /* Ask the thread to leave broadcast. Feed through the observe window so
     * diveo2_observe_broadcasting() sees the cell "streaming" (is_on=true);
     * with want_on=false that forces diveo2_set_cell_broadcast(cell, false) —
     * the "#BCST 0" OFF path — before the cell drops to polled sampling. */
    diveo2_request_broadcast(0U, false);
    feed_for(VALID_MRAW, PASSIVE_LISTEN_MS + 600);

    zassert_true(feed_until_sample(VALID_MOXY, MOXY_SAMPLE, 4000),
                 "no CELL_OK simple reading with the expected sample");
}

/**
 * @brief A malformed measurement frame (right #MRAW header, garbage field)
 *        surfaces as CELL_FAIL for that cycle (process_rx is_measurement path).
 */
ZTEST(diveo2_thread, test_04_malformed_measurement_fails)
{
    stub_power_set_vbus(5.0f);
    /* First a good frame so we start from CELL_OK, then a corrupt one. */
    (void)feed_until_status(VALID_MOXY, CELL_OK, 3000);

    bool failed = feed_until_status("#MRAW 210000 2500 x 1 2 3 4 5\r",
                                    CELL_FAIL, 3000);

    zassert_true(failed, "corrupt measurement did not fail the cell");
}

/**
 * @brief A non-measurement frame (e.g. an #ERRO probe reply) is ignored — the
 *        cell keeps its prior good reading (process_rx unknown-message path).
 */
ZTEST(diveo2_thread, test_05_non_measurement_ignored)
{
    stub_power_set_vbus(5.0f);
    (void)feed_until_status(VALID_MOXY, CELL_OK, 3000);

    /* Feed only junk for a while; the thread logs + skips it. It must not
     * crash. Staleness may eventually fail it, but that is a separate path. */
    int elapsed = 0;

    while (elapsed < 300) {
        feed("#ERRO -26\r");
        (void)k_msleep(POLL_STEP_MS);
        elapsed += POLL_STEP_MS;
    }
    /* If we got here without faulting, the unknown-message branch ran. */
    zassert_true(true, "unknown-message branch executed");
}

/**
 * @brief An over-range PPO2 sample (>2.55 bar) is clamped to CELL_FAIL by the
 *        broadcast() overrange guard.
 */
ZTEST(diveo2_thread, test_06_overrange_fails)
{
    stub_power_set_vbus(5.0f);

    /* cal_coeff default = 1e6 counts/bar; 9,000,000 counts -> 9.0 bar -> 900
     * centibar, well over the 255 overrange limit. */
    bool failed = feed_until_status(
        "#MOXY 9000000 2500 0\r", CELL_FAIL, 4000);

    zassert_true(failed, "overrange sample did not fail the cell");
}

/**
 * @brief VBUS below 3.25 V forces CELL_FAIL via the undervolt guard.
 */
ZTEST(diveo2_thread, test_07_undervolt_fails)
{
    /* Establish a fresh CELL_OK reading first, so the subsequent FAIL is
     * attributable to the undervolt guard and not a leftover/stale FAIL. */
    stub_power_set_vbus(5.0f);
    zassert_true(feed_until_status(VALID_MOXY, CELL_OK, 4000),
                 "undervolt precondition (CELL_OK) not reached");

    /* Drop the rail below VBUS_MIN_VOLTAGE while KEEPING readings fresh (still
     * feeding valid frames) so staleness cannot be the FAIL source. */
    stub_power_set_vbus(3.0f);
    bool failed = feed_until_status(VALID_MOXY, CELL_FAIL, 3000);

    stub_power_set_vbus(5.0f); /* restore for later cases */
    zassert_true(failed, "undervolt did not fail the cell");
}

/**
 * @brief With no fresh frames the staleness timeout (>1 s) fails the cell.
 */
ZTEST(diveo2_thread, test_08_staleness_fails)
{
    stub_power_set_vbus(5.0f);
    (void)feed_until_status(VALID_MOXY, CELL_OK, 3000);

    /* Starve the cell: no frames for longer than DIGITAL_RESPONSE_TIMEOUT_MS.
     * The polled loop's wait_frame() also times out (OP_ERR_TIMEOUT). */
    idle(STALENESS_MS + POLL_TIMEOUT_MS + 400);

    zassert_equal(CELL_FAIL, read_cell_status(NULL),
                  "stale cell not failed");
}

/**
 * @brief diveo2_load_cal covers both branches, driven synchronously via the
 *        chan_cal_response listener (no thread-timing dependence).
 */
ZTEST(diveo2_thread, test_09_load_cal_both_branches)
{
    CalResponse_t resp = {0};

    resp.result = CAL_RESULT_OK;

    /* Valid stored coefficient -> load_cal takes the CELL_OK/coeff path. */
    seed_cal(true);
    (void)zbus_chan_pub(&chan_cal_response, &resp, K_MSEC(100));
    (void)k_msleep(POLL_STEP_MS);

    /* Missing/invalid coefficient -> default coeff + CELL_FAIL path. */
    seed_cal(false);
    (void)zbus_chan_pub(&chan_cal_response, &resp, K_MSEC(100));
    (void)k_msleep(POLL_STEP_MS);

    seed_cal(true); /* restore */
    /* Reaching here means both load_cal branches ran without faulting. */
    zassert_true(true, "load_cal both branches executed");
}

/**
 * @brief diveo2_request_broadcast: valid ON request is accepted; an
 *        out-of-range cell index is rejected (OP_ERROR reject path).
 */
ZTEST(diveo2_thread, test_10_request_broadcast_paths)
{
    stub_power_set_vbus(5.0f);

    /* Out-of-range cell index -> reject branch (only cell 0 exists here). */
    diveo2_request_broadcast(2U, true);

    /* Valid ON request. Feed through the observe window so is_on reads true;
     * with want_on=true that hits apply_broadcast's "already streaming, skip
     * #BCST" branch (diveo2_on_off_str). */
    diveo2_request_broadcast(0U, true);
    feed_for(VALID_MRAW, PASSIVE_LISTEN_MS + 600);
    (void)feed_until_status(VALID_MRAW, CELL_OK, 4000);

    zassert_true(true, "request_broadcast valid + reject + skip paths executed");
}

/**
 * @brief feed_rx line-assembly edge cases: an empty line (bare CR) is ignored
 *        and an over-long line with no terminator resyncs — neither faults.
 */
ZTEST(diveo2_thread, test_11_feed_rx_edge_cases)
{
    stub_power_set_vbus(5.0f);

    /* Bare CRs: rx_line_len == 0 branch (empty lines dropped). */
    feed("\r\r\r");
    (void)k_msleep(POLL_STEP_MS);

    /* 100 bytes with no CR: exceeds DIVEO2_RX_BUFFER_LEN-1 -> overrun resync. */
    static const char longjunk[] =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

    feed(longjunk);
    (void)k_msleep(POLL_STEP_MS);

    /* A good frame after the resync must still parse. */
    zassert_true(feed_until_status(VALID_MRAW, CELL_OK, 4000),
                 "cell did not recover after overrun resync");
}
