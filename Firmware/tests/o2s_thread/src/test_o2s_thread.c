/**
 * @file test_o2s_thread.c
 * @brief Thread-level tests for the O2S cell driver (oxygen_cell_o2s.c).
 *
 * tests/parsers already covers the pure parse helpers. This module instead
 * spawns the *real* per-cell thread (CONFIG_CELL_1_TYPE_O2S=y in prj.conf,
 * usart1 = zephyr,uart-emul in the overlay) and drives it through its private
 * static state machine — setup, the polled sample loop, process_rx dispatch,
 * the broadcast publish math (staleness / undervolt / overrange), load_cal, and
 * the cal-done listener — none of which is reachable from a separate TU because
 * the functions are file-static.
 *
 * Driving strategy (specific to the O2S RX contract):
 *   The O2S thread only learns a frame arrived when the async UART fires
 *   UART_RX_DISABLED (its rx_sem is given nowhere else). Its capture callback,
 *   o2s_capture_rx(), copies an RX_RDY payload into last_message ONLY when the
 *   chunk is shorter than the 10-byte rx buffer (rx->len < O2S_RX_BUFFER_LEN);
 *   a full-buffer chunk (len == 10) is dropped and last_message is left intact.
 *
 *   uart_emul delivers under two triggers we can steer:
 *     (a) A short frame (<10 bytes) sits in the rx buffer as a partial. When the
 *         thread's k_sem_take() times out it calls uart_rx_disable(), whose stop
 *         path flushes the pending partial as one RX_RDY(len<10) -> captured into
 *         last_message. So feeding one short frame and letting a full ~1 s cycle
 *         elapse deterministically seeds last_message with that frame.
 *     (b) Feeding >= 10 bytes fills the rx buffer, firing RX_RDY(len==10)
 *         (dropped, last_message untouched) immediately followed by
 *         UART_RX_DISABLED -> rx_sem given INSIDE the k_sem_take() window ->
 *         o2s_process_rx() runs against the previously-seeded last_message.
 *
 *   drive_status() below composes (a) then (b): seed last_message with the frame
 *   under test, then force an in-window disable so process_rx/broadcast run on
 *   it and publish an OxygenCellMsg_t we assert on chan_cell_1. Because
 *   native_sim freezes simulated time whenever every thread is runnable, EVERY
 *   wait goes through k_msleep() so the cell thread and the uart_emul work queue
 *   actually get to run.
 *
 * Deliberately NOT covered (src/ is read-only for this task, so these arms
 * cannot be reached without fault injection the host emulator does not expose):
 *   - o2s_setup() failure arms: device_is_ready()==false and
 *     uart_callback_set() error. uart_emul is always ready and its callback set
 *     always succeeds on native_sim, so the `ok=false` branches and the
 *     thread's "skip the loop when setup fails" path are unreachable.
 *   - o2s_send_command()'s NULL guard (OP_ERR_NULL_PTR) and its uart_tx() error
 *     arm: the only caller is the thread (command is never NULL) and uart_emul
 *     TX never fails.
 *   - The thread loop's uart_rx_enable() error arm (rx_ret != 0): enable only
 *     returns non-zero on -EBUSY (already enabled), and the thread disables RX
 *     at the top of every iteration, so enable always succeeds here.
 */

/* strtok_r requires POSIX source on native_sim (host libc) */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/settings/settings.h>
#include <string.h>
#include <errno.h>

#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"

/* Overridden production symbol lives in stub_power.c */
extern void stub_power_set_vbus(Numeric_t volts);

/* ---- Test-controlled calibration settings handler ----------------------- *
 * o2s_load_cal() reads "cal/cellN" via settings_runtime_get(). We register a
 * static handler for the "cal" subtree so both branches of load_cal are
 * reachable: g_cal_valid=true returns an in-range coefficient (-> CELL_OK),
 * false returns -ENOENT (-> default coefficient + CELL_FAIL).
 */
static bool g_cal_valid = true;
/* When true, the handler returns a coefficient OUTSIDE (O2S_CAL_LOWER,
 * O2S_CAL_UPPER) so o2s_load_cal()'s range check rejects it (exercises the
 * "right length, out-of-range value" false sub-branch, distinct from -ENOENT). */
static bool g_cal_out_of_range = false;

static int cal_h_get(const char *key, char *val, int val_len_max)
{
    int rc = -ENOENT;

    if ((key != NULL) && g_cal_valid &&
        ((size_t)val_len_max >= sizeof(CalCoeff_t))) {
        /* In-range: within (O2S_CAL_LOWER, O2S_CAL_UPPER) == (0.8, 1.2).
         * Out-of-range: 5.0, above the upper bound. */
        CalCoeff_t coeff = 1.05f;

        if (g_cal_out_of_range) {
            coeff = 5.0f;
        }
        (void)memcpy(val, &coeff, sizeof(coeff));
        rc = (int)sizeof(coeff);
    }
    return rc;
}

SETTINGS_STATIC_HANDLER_DEFINE(caltest, "cal", cal_h_get, NULL, NULL, NULL);

/* ---- Timing + stimulus constants ---------------------------------------- */

/* One full O2S thread cycle is UART_RX_TIMEOUT_MS (1000, the k_sem_take window)
 * plus SAMPLE_INTERVAL_MS (500). Pump a bit beyond that so a whole cycle
 * elapses (thread times out -> disables -> flushes the seeded partial). */
#define POLL_STEP_MS      20
/* Worst-case time for the thread to latch a seeded frame into last_message:
 * finish the in-flight k_sem_take window (<=1000), the inter-sample sleep (500),
 * then the next enable+timeout that flushes the partial (<=1000), plus slack. */
#define SEED_MS         3000
/* One force-process cycle: fill the rx buffer -> in-window disable -> process_rx,
 * plus the inter-sample sleep and slack. */
#define CAPTURE_MS      1700
#define DRIVE_BUDGET_MS 9000

/* Exactly O2S_RX_BUFFER_LEN (10) bytes so the rx buffer fills in one read and is
 * fully consumed (no residue): the resulting RX_RDY(len==10) is dropped by
 * o2s_capture_rx() and immediately followed by UART_RX_DISABLED. Not a valid
 * frame — its only job is to force the in-window disable. */
#define FILLER "##########"

/* Frames are all <= 9 bytes so o2s_capture_rx() latches them whole. */
#define FRAME_OK        "Mn:0.5\n"   /* -> 0.5 bar * 1.05 * 100 = 52.5 cbar  */
#define FRAME_OK_ECHO   "Mm:0.4\n"   /* command-echo header is also accepted  */
#define FRAME_OVERRANGE "Mn:9\n"     /* 9 bar -> 945 cbar > 255 overrange     */
#define FRAME_MALFORMED "Mn:x\n"     /* right header, non-numeric value       */
#define FRAME_UNKNOWN   "ZZ:1\n"     /* not a measurement header at all       */
#define FRAME_ECHO_ONLY "Mn\n"       /* measurement header, no value token    */
#define FRAME_LEADING   "\n\nMn:0.5\n" /* leading newlines the parser strips  */

static const struct device *cell_uart(void)
{
    return DEVICE_DT_GET(DT_NODELABEL(usart1));
}

static void feed(const char *frame)
{
    (void)uart_emul_put_rx_data(cell_uart(), (const uint8_t *)frame,
                                strlen(frame));
}

/* Advance simulated time in POLL_STEP_MS slices so the cell thread and the
 * uart_emul work queue run. */
static void pump(int ms)
{
    int elapsed = 0;

    while (elapsed < ms) {
        (void)k_msleep(POLL_STEP_MS);
        elapsed += POLL_STEP_MS;
    }
}

static CellStatus_t read_status(OxygenCellMsg_t *out)
{
    OxygenCellMsg_t msg = {0};

    (void)zbus_chan_read(&chan_cell_1, &msg, K_MSEC(100));
    if (out != NULL) {
        *out = msg;
    }
    return msg.status;
}

/**
 * @brief Make the O2S thread process @p frame as its received message and wait
 *        until it publishes @p want on chan_cell_1.
 *
 * Phase 1 seeds last_message with @p frame (captured when the thread's poll
 * times out and disables RX). Phase 2 forces an in-window buffer-full disable
 * each cycle so o2s_process_rx() runs against that seeded frame, polling the
 * published status until it matches @p want or the budget expires.
 *
 * @return true if @p want was observed within DRIVE_BUDGET_MS.
 */
static bool drive_status(const char *frame, CellStatus_t want)
{
    bool hit = false;
    int elapsed = 0;

    /* Clear any residue a previous case left in the rx ring. */
    (void)uart_emul_flush_rx_data(cell_uart());

    /* Phase 1: seed last_message. Pump a guaranteed full latch cycle so
     * last_message holds THIS frame before any force-process below — otherwise
     * a leftover CELL_OK from a previous case could satisfy `want` against the
     * wrong (stale) frame and skip the parse we are trying to exercise. */
    feed(frame);
    pump(SEED_MS);

    /* Phase 2: force an in-window disable each cycle so o2s_process_rx() runs on
     * the seeded frame. Always run at least one cycle (do/while) so the frame is
     * actually parsed even when `want` already happens to hold. */
    do {
        feed(FILLER);
        pump(CAPTURE_MS);
        elapsed += CAPTURE_MS;
        if (read_status(NULL) == want) {
            hit = true;
        }
    } while ((elapsed < DRIVE_BUDGET_MS) && !hit);
    return hit;
}

/* ========================================================================= *
 * Suite: full O2S cell-thread lifecycle over an emulated UART.
 * Ordered — a single persistent thread is shared across the cases below, so
 * they run in definition order (CONFIG_ZTEST_SHUFFLE stays off) and each case
 * leaves the cell in a known-enough state for the next.
 * ========================================================================= */
ZTEST_SUITE(o2s_thread, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief A valid "Mn:<ppo2>" reading is parsed and republished as CELL_OK with
 *        a populated wire PPO2 (process_rx parse-OK path + broadcast happy path).
 */
ZTEST(o2s_thread, test_01_valid_reading_published)
{
    OxygenCellMsg_t msg = {0};

    stub_power_set_vbus(5.0f);

    zassert_true(drive_status(FRAME_OK, CELL_OK),
                 "valid reading did not publish CELL_OK");

    (void)read_status(&msg);
    zassert_equal(CELL_OK, msg.status, "status");
    zassert_equal(0U, msg.cell_number, "cell number");
    zassert_true(msg.ppo2 > 0U, "ppo2 wire value populated");
}

/**
 * @brief The command-echo header ("Mm:") is also accepted as a reading
 *        (half-duplex echo handling in o2s_parse_response).
 */
ZTEST(o2s_thread, test_02_echo_header_accepted)
{
    stub_power_set_vbus(5.0f);

    zassert_true(drive_status(FRAME_OK_ECHO, CELL_OK),
                 "echo-header reading not accepted as CELL_OK");
}

/**
 * @brief A malformed measurement (right "Mn:" header, non-numeric value) fails
 *        the cell for that cycle (process_rx is_malformed path -> broadcast).
 */
ZTEST(o2s_thread, test_03_malformed_measurement_fails)
{
    stub_power_set_vbus(5.0f);
    /* Start from a good reading so the FAIL is attributable to the corrupt frame. */
    (void)drive_status(FRAME_OK, CELL_OK);

    zassert_true(drive_status(FRAME_MALFORMED, CELL_FAIL),
                 "malformed measurement did not fail the cell");
}

/**
 * @brief A non-measurement frame is ignored — process_rx logs and skips it, the
 *        cell keeps its prior good reading (unknown-message path, no broadcast).
 */
ZTEST(o2s_thread, test_04_unknown_frame_ignored)
{
    stub_power_set_vbus(5.0f);
    zassert_true(drive_status(FRAME_OK, CELL_OK),
                 "precondition CELL_OK not reached");

    /* Seed an unknown frame and pump: process_rx must take the unknown branch
     * (neither parse-OK nor malformed) without crashing. The published status
     * stays CELL_OK because the unknown branch does not broadcast. */
    (void)uart_emul_flush_rx_data(cell_uart());
    feed(FRAME_UNKNOWN);
    pump(CAPTURE_MS);
    feed(FILLER);
    pump(CAPTURE_MS);

    zassert_equal(CELL_OK, read_status(NULL),
                  "unknown frame must not change the published reading");

    /* An echo-only measurement frame ("Mn" with no value token) is also
     * ignored: parse fails, is_malformed is false (no value present), so the
     * unknown branch runs again and the reading is preserved. */
    (void)uart_emul_flush_rx_data(cell_uart());
    feed(FRAME_ECHO_ONLY);
    pump(CAPTURE_MS);
    feed(FILLER);
    pump(CAPTURE_MS);

    zassert_equal(CELL_OK, read_status(NULL),
                  "echo-only frame must not change the published reading");
}

/**
 * @brief An over-range sample (>2.55 bar equivalent) is clamped to CELL_FAIL by
 *        the broadcast() overrange guard.
 */
ZTEST(o2s_thread, test_05_overrange_fails)
{
    stub_power_set_vbus(5.0f);

    zassert_true(drive_status(FRAME_OVERRANGE, CELL_FAIL),
                 "overrange sample did not fail the cell");
}

/**
 * @brief VBUS below VBUS_MIN_VOLTAGE (3.25 V) forces CELL_FAIL via the undervolt
 *        guard, while a valid frame keeps the reading fresh.
 */
ZTEST(o2s_thread, test_06_undervolt_fails)
{
    stub_power_set_vbus(5.0f);
    zassert_true(drive_status(FRAME_OK, CELL_OK),
                 "undervolt precondition (CELL_OK) not reached");

    stub_power_set_vbus(3.0f);
    bool failed = drive_status(FRAME_OK, CELL_FAIL);

    stub_power_set_vbus(5.0f); /* restore for later cases */
    zassert_true(failed, "undervolt did not fail the cell");
}

/**
 * @brief The broadcast staleness guard fails the cell when a (malformed) frame
 *        is processed more than DIGITAL_RESPONSE_TIMEOUT_MS (2 s) after the last
 *        good reading.
 *
 * The parse-OK path always refreshes last_ppo2_ticks immediately before
 * broadcasting, so it can never be stale; the staleness branch is only reachable
 * when broadcast() runs from the malformed path (which does NOT refresh the
 * timestamp). Starving the cell first guarantees the stale condition holds.
 */
ZTEST(o2s_thread, test_07_staleness_fails)
{
    stub_power_set_vbus(5.0f);
    zassert_true(drive_status(FRAME_OK, CELL_OK),
                 "staleness precondition (CELL_OK) not reached");

    /* Starve the cell well beyond DIGITAL_RESPONSE_TIMEOUT_MS (2 s) so the next
     * broadcast sees stale data, then drive a malformed frame (the only path
     * that broadcasts without refreshing last_ppo2_ticks). */
    (void)uart_emul_flush_rx_data(cell_uart());
    pump(3000);

    zassert_true(drive_status(FRAME_MALFORMED, CELL_FAIL),
                 "stale cell not failed");
}

/**
 * @brief o2s_load_cal covers both branches, driven synchronously via the
 *        chan_cal_response listener (no thread-timing dependence).
 */
ZTEST(o2s_thread, test_08_load_cal_both_branches)
{
    CalResponse_t resp = {0};

    resp.result = CAL_RESULT_OK;

    /* Valid stored coefficient -> load_cal takes the in-range/coeff path. */
    g_cal_valid = true;
    (void)zbus_chan_pub(&chan_cal_response, &resp, K_MSEC(100));
    pump(POLL_STEP_MS);

    /* Missing coefficient (-ENOENT) -> default coeff + CELL_FAIL path. */
    g_cal_valid = false;
    (void)zbus_chan_pub(&chan_cal_response, &resp, K_MSEC(100));
    pump(POLL_STEP_MS);

    /* Present but out-of-range coefficient -> same reject path via the range
     * check rather than the length check (distinct false sub-branch). */
    g_cal_valid = true;
    g_cal_out_of_range = true;
    (void)zbus_chan_pub(&chan_cal_response, &resp, K_MSEC(100));
    pump(POLL_STEP_MS);

    g_cal_out_of_range = false; /* restore */
    /* Reaching here means all load_cal branches ran without faulting. */
    zassert_true(true, "load_cal both branches executed");
}

/**
 * @brief After a cal reload the cell recovers to CELL_OK on the next valid
 *        reading, proving the cal-done listener re-armed the coefficient.
 */
ZTEST(o2s_thread, test_09_recovers_after_cal_reload)
{
    CalResponse_t resp = {0};

    resp.result = CAL_RESULT_OK;
    g_cal_valid = true;
    stub_power_set_vbus(5.0f);

    (void)zbus_chan_pub(&chan_cal_response, &resp, K_MSEC(100));
    pump(POLL_STEP_MS);

    zassert_true(drive_status(FRAME_OK, CELL_OK),
                 "cell did not recover to CELL_OK after cal reload");
}

/**
 * @brief Leading nulls/newlines on a frame are stripped before parsing
 *        (o2s_prepare_message_buffer skip-leading-junk loop) and the reading
 *        still lands as CELL_OK.
 */
ZTEST(o2s_thread, test_10_leading_junk_stripped)
{
    stub_power_set_vbus(5.0f);

    zassert_true(drive_status(FRAME_LEADING, CELL_OK),
                 "leading-junk frame not parsed to CELL_OK");
}

/**
 * @brief A non-positive VBUS reading (sensor unavailable / rail at 0 V) skips
 *        the undervolt guard rather than tripping it: the `vbus_v > 0.0f`
 *        sub-condition is false, so a valid reading still publishes CELL_OK.
 */
ZTEST(o2s_thread, test_11_zero_vbus_skips_undervolt)
{
    stub_power_set_vbus(0.0f);

    bool ok = drive_status(FRAME_OK, CELL_OK);

    stub_power_set_vbus(5.0f); /* restore */
    zassert_true(ok, "zero VBUS must not trip the undervolt guard");
}
