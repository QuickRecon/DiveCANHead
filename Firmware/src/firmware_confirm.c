/**
 * @file firmware_confirm.c
 * @brief POST gate that confirms or rolls back a freshly-swapped image.
 *
 * See firmware_confirm.h for the high-level contract. This TU is the
 * actual state machine: a single K_THREAD_DEFINE'd thread polls every
 * dependency in turn (oxygen cells, voted consensus, CAN TX counter,
 * handset RX counters, solenoid) and either calls
 * boot_write_img_confirmed() on full pass or sys_reboot()s without
 * confirming on any sub-failure or overall deadline expiry.
 *
 * The polling cadence (POLL_INTERVAL_MS) is set so the thread doesn't
 * starve lower-priority threads but reacts within ~50 ms of a published
 * channel update — fast enough that the wall-clock deadline budget
 * isn't perceptibly inflated by the polling resolution.
 *
 * Tests build the same source with CONFIG_ZTEST=y to expose
 * firmware_confirm_run_sync_for_test(), which runs the state machine
 * inline (no thread, no auto-init). That makes test scaffolding around
 * synthetic zbus publishes deterministic.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>

#include "firmware_confirm.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "divecan/include/divecan_channels.h"
#include "divecan/include/divecan_types.h"
#include "divecan/include/divecan_counters.h"
#ifdef CONFIG_FACTORY_IMAGE
#include "factory_image.h"
#endif
#include "errors.h"

LOG_MODULE_REGISTER(fw_confirm, LOG_LEVEL_INF);

/* ---- Constants ---- */

/** @brief How often the POST thread polls zbus + F-section counters. */
#define POLL_INTERVAL_MS    50U

/** @brief How long to allow a zbus_chan_read snapshot to wait for the lock. */
#define ZBUS_READ_TIMEOUT_MS  10

/** @brief Overall POST deadline. */
#define POST_DEADLINE_MS   ((int64_t)CONFIG_FIRMWARE_CONFIRM_DEADLINE_MS)

/** @brief Per-check deadlines (clamped to remaining overall budget). */
#define POST_CELL_DEADLINE_MS       ((int64_t)CONFIG_FIRMWARE_CONFIRM_CELL_TIMEOUT_MS)
#define POST_CONSENSUS_DEADLINE_MS  ((int64_t)CONFIG_FIRMWARE_CONFIRM_CONSENSUS_TIMEOUT_MS)
#define POST_PPO2_TX_DEADLINE_MS    ((int64_t)CONFIG_FIRMWARE_CONFIRM_PPO2_TX_TIMEOUT_MS)
#define POST_HANDSET_DEADLINE_MS    ((int64_t)CONFIG_FIRMWARE_CONFIRM_HANDSET_TIMEOUT_MS)
#define POST_SOL_DEADLINE_MS        ((int64_t)CONFIG_FIRMWARE_CONFIRM_SOL_TIMEOUT_MS)

/** @brief Delay before sys_reboot() so a fatal log line can flush. */
#define POST_REBOOT_DELAY_MS  200

/** @brief POST thread stack size (B). */
#define POST_THREAD_STACK   2048
/** @brief POST thread priority (between divecan_rx prio 5 and watchdog prio 14). */
#define POST_THREAD_PRIO    7

/* ---- File-scope state ----
 *
 * Atomic primitives so accessor functions don't need a mutex; the POST
 * thread is the sole writer for state/pass_mask, every other reader is
 * a snapshot via atomic_get().
 */

static atomic_t s_post_state = (atomic_val_t)POST_INIT;
static atomic_t s_post_pass_mask;

/* ---- Helpers ---- */

/**
 * @brief Promote a check state, taking the bit-mask along with it.
 */
static void mark_check_passed(uint32_t bit, PostState_t next_state)
{
    (void)atomic_or(&s_post_pass_mask, (atomic_val_t)BIT(bit));
    (void)atomic_set(&s_post_state, (atomic_val_t)next_state);
}

/**
 * @brief Read a per-cell snapshot, returning true if it passes the gate.
 *
 * A cell passes when its most-recent reading carries CELL_OK or
 * CELL_NEED_CAL (i.e. anything other than CELL_FAIL and CELL_DEGRADED
 * — degraded cells must recover before POST trusts them). A
 * never-published channel returns the default CELL_FAIL initial value
 * and stays failed.
 */
static bool cell_is_alive(const struct zbus_channel *chan)
{
    OxygenCellMsg_t msg = {0};
    bool alive = false;
    int rc = zbus_chan_read(chan, &msg, K_MSEC(ZBUS_READ_TIMEOUT_MS));

    if (0 == rc) {
        if ((CELL_OK == msg.status) || (CELL_NEED_CAL == msg.status)) {
            alive = true;
        }
    }
    return alive;
}

/**
 * @brief Read the voted consensus, returning true if it passes the gate.
 */
static bool consensus_is_alive(void)
{
    ConsensusMsg_t msg = {0};
    bool alive = false;
    int rc = zbus_chan_read(&chan_consensus, &msg, K_MSEC(ZBUS_READ_TIMEOUT_MS));

    if (0 == rc) {
        if (PPO2_FAIL != msg.consensus_ppo2) {
            alive = true;
        }
    }
    return alive;
}

/**
 * @brief Read the solenoid status, returning true if it passes the gate.
 */
static bool solenoid_is_alive(void)
{
    DiveCANError_t status = DIVECAN_ERR_NONE;
    bool alive = false;
#ifdef CONFIG_HAS_O2_SOLENOID
    int rc = zbus_chan_read(&chan_solenoid_status, &status,
                            K_MSEC(ZBUS_READ_TIMEOUT_MS));
    if (0 == rc) {
        if (DIVECAN_ERR_SOL_NORM == status) {
            alive = true;
        }
    }
#else
    (void)status;
    alive = true;
#endif
    return alive;
}

/**
 * @brief Sum of BUS_INIT + BUS_ID RX counters seen since boot.
 *
 * Saturates at UINT32_MAX (each accessor is itself saturating), so the
 * arithmetic here can't overflow.
 */
static uint32_t handset_rx_total(void)
{
    uint32_t init_count = divecan_rx_get_bus_init_count();
    uint32_t id_count = divecan_rx_get_bus_id_count();
    uint64_t sum = (uint64_t)init_count + (uint64_t)id_count;

    if (sum > UINT32_MAX) {
        sum = UINT32_MAX;
    }
    return (uint32_t)sum;
}

/**
 * @brief Time remaining inside the overall deadline window, in ms.
 *
 * Negative values mean the deadline has already passed; callers must
 * fail out before any further work.
 */
static int64_t deadline_remaining_ms(int64_t start_ms)
{
    int64_t elapsed = k_uptime_get() - start_ms;
    return POST_DEADLINE_MS - elapsed;
}

/**
 * @brief Stamp a fail reason into the histogram and reboot without confirm.
 *
 * The reboot path is what triggers MCUBoot rollback on the next swap.
 * The 200 ms sleep before sys_reboot is so the LOG_ERR line has time
 * to drain to RTT/UART before we yank the CPU.
 */
static void abort_and_reboot(PostState_t fail_state)
{
    (void)atomic_set(&s_post_state, (atomic_val_t)fail_state);
    OP_ERROR_DETAIL(OP_ERR_POST_FAIL, (uint32_t)fail_state);
    LOG_ERR("POST failed in state %d — rebooting (no confirm)", (int)fail_state);

#ifdef CONFIG_ZTEST
    /* In test builds the wrap stub takes over and notes the call; don't
     * burn wall time on a sleep we don't need. */
    sys_reboot(SYS_REBOOT_COLD);
#else
    k_msleep(POST_REBOOT_DELAY_MS);
    sys_reboot(SYS_REBOOT_COLD);
#endif
}

/* ---- SMF state machine ----
 *
 * The POST gate is a periodic-tick state machine. Each WAITING_*
 * state's `run` checks its subsystem predicate once and:
 *   - on pass: marks the corresponding pass-bit and transitions to the
 *     next WAITING_* state;
 *   - on overall deadline exceeded: transitions to POST_FAILED_TIMEOUT;
 *   - on per-state budget exceeded (when overall still has time):
 *     transitions to the matching POST_FAILED_<this> state;
 *   - otherwise stays put — the driver loop will k_msleep(POLL_INTERVAL_MS)
 *     and call smf_run_state again on the next tick.
 *
 * POST_CONFIRMED.entry calls boot_write_img_confirmed and terminates.
 * POST_FAILED_*.entry calls abort_and_reboot, which never returns
 * (production) or longjmps out via the wrapped sys_reboot (tests).
 */

typedef struct {
    struct smf_ctx smf;
    int64_t        start_ms;        /* wall-clock POST start */
    int64_t        state_start_ms;  /* per-state deadline anchor */
    uint32_t       tx_baseline;     /* divecan_send_get_tx_count() at start */
    uint32_t       rx_baseline;     /* handset_rx_total() at start */
} PostSmCtx_t;

static const struct smf_state post_states[];

BUILD_ASSERT(POST_INIT == 0, "post_states[] index must match enum");

/**
 * @brief Stamp a WAITING_* state into the atomic + reset per-state deadline.
 *
 * Shared shape across every waiting state's entry — moving it here
 * keeps the table rows symmetrical.
 */
static void enter_waiting_state(PostSmCtx_t *sm, PostState_t state)
{
    sm->state_start_ms = k_uptime_get();
    (void)atomic_set(&s_post_state, (atomic_val_t)state);
}

/**
 * @brief Returns true once the overall POST deadline has elapsed.
 */
static bool overall_deadline_exceeded(const PostSmCtx_t *sm)
{
    return deadline_remaining_ms(sm->start_ms) <= 0;
}

/**
 * @brief Returns true once the per-state budget (raw, unclamped) has elapsed.
 *
 * The overall-deadline check runs first in each run action, so the
 * effective per-state budget is implicitly MIN(per-state, overall
 * remaining) — same semantics as the legacy clamp_budget helper.
 */
static bool state_budget_exceeded(const PostSmCtx_t *sm, int64_t budget_ms)
{
    return (k_uptime_get() - sm->state_start_ms) >= budget_ms;
}

/**
 * @brief Predicate: are all compiled-in cells healthy?
 */
static bool all_cells_alive(void)
{
    bool c1 = cell_is_alive(&chan_cell_1);
#if CONFIG_CELL_COUNT >= 2
    bool c2 = cell_is_alive(&chan_cell_2);
#else
    bool c2 = true;
#endif
#if CONFIG_CELL_COUNT >= 3
    bool c3 = cell_is_alive(&chan_cell_3);
#else
    bool c3 = true;
#endif
    return c1 && c2 && c3;
}

/* ---- WAITING state entries: stamp atomic + deadline ---- */

static void post_cells_entry(void *obj)
{
    enter_waiting_state((PostSmCtx_t *)obj, POST_WAITING_CELLS);
}

static void post_consensus_entry(void *obj)
{
    enter_waiting_state((PostSmCtx_t *)obj, POST_WAITING_CONSENSUS);
}

static void post_ppo2_tx_entry(void *obj)
{
    enter_waiting_state((PostSmCtx_t *)obj, POST_WAITING_PPO2_TX);
}

static void post_handset_entry(void *obj)
{
    enter_waiting_state((PostSmCtx_t *)obj, POST_WAITING_HANDSET);
}

static void post_solenoid_entry(void *obj)
{
    enter_waiting_state((PostSmCtx_t *)obj, POST_WAITING_SOLENOID);
}

/* ---- WAITING state run functions ---- */

static enum smf_state_result post_cells_run(void *obj)
{
    PostSmCtx_t *sm = (PostSmCtx_t *)obj;

    if (all_cells_alive()) {
        mark_check_passed(POST_PASS_BIT_CELLS, POST_WAITING_CONSENSUS);
        smf_set_state(SMF_CTX(sm), &post_states[POST_WAITING_CONSENSUS]);
    } else if (overall_deadline_exceeded(sm)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_TIMEOUT]);
    } else if (state_budget_exceeded(sm, POST_CELL_DEADLINE_MS)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_CELL]);
    } else {
        /* Stay; next tick. */
    }
    return SMF_EVENT_HANDLED;
}

static enum smf_state_result post_consensus_run(void *obj)
{
    PostSmCtx_t *sm = (PostSmCtx_t *)obj;

    if (consensus_is_alive()) {
        mark_check_passed(POST_PASS_BIT_CONSENSUS, POST_WAITING_PPO2_TX);
        smf_set_state(SMF_CTX(sm), &post_states[POST_WAITING_PPO2_TX]);
    } else if (overall_deadline_exceeded(sm)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_TIMEOUT]);
    } else if (state_budget_exceeded(sm, POST_CONSENSUS_DEADLINE_MS)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_CONSENSUS]);
    } else {
        /* Stay; next tick. */
    }
    return SMF_EVENT_HANDLED;
}

static enum smf_state_result post_ppo2_tx_run(void *obj)
{
    PostSmCtx_t *sm = (PostSmCtx_t *)obj;
    uint32_t now_count = divecan_send_get_tx_count();
    uint32_t delta = now_count - sm->tx_baseline;

    if (delta >= POST_REQUIRED_PPO2_TX_COUNT) {
        mark_check_passed(POST_PASS_BIT_PPO2_TX, POST_WAITING_HANDSET);
        smf_set_state(SMF_CTX(sm), &post_states[POST_WAITING_HANDSET]);
    } else if (overall_deadline_exceeded(sm)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_TIMEOUT]);
    } else if (state_budget_exceeded(sm, POST_PPO2_TX_DEADLINE_MS)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_NO_PPO2_TX]);
    } else {
        /* Stay; next tick. */
    }
    return SMF_EVENT_HANDLED;
}

static enum smf_state_result post_handset_run(void *obj)
{
    PostSmCtx_t *sm = (PostSmCtx_t *)obj;
    uint32_t now_total = handset_rx_total();

    if (now_total > sm->rx_baseline) {
        mark_check_passed(POST_PASS_BIT_HANDSET, POST_WAITING_SOLENOID);
        smf_set_state(SMF_CTX(sm), &post_states[POST_WAITING_SOLENOID]);
    } else if (overall_deadline_exceeded(sm)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_TIMEOUT]);
    } else if (state_budget_exceeded(sm, POST_HANDSET_DEADLINE_MS)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_NO_HANDSET]);
    } else {
        /* Stay; next tick. */
    }
    return SMF_EVENT_HANDLED;
}

static enum smf_state_result post_solenoid_run(void *obj)
{
    PostSmCtx_t *sm = (PostSmCtx_t *)obj;

    if (solenoid_is_alive()) {
        mark_check_passed(POST_PASS_BIT_SOLENOID, POST_CONFIRMED);
        smf_set_state(SMF_CTX(sm), &post_states[POST_CONFIRMED]);
    } else if (overall_deadline_exceeded(sm)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_TIMEOUT]);
    } else if (state_budget_exceeded(sm, POST_SOL_DEADLINE_MS)) {
        smf_set_state(SMF_CTX(sm), &post_states[POST_FAILED_SOLENOID]);
    } else {
        /* Stay; next tick. */
    }
    return SMF_EVENT_HANDLED;
}

/* ---- Terminal state entries ---- */

static void post_confirmed_entry(void *obj)
{
    PostSmCtx_t *sm = (PostSmCtx_t *)obj;

#ifndef CONFIG_ZTEST
    int rc = boot_write_img_confirmed();
    if (0 != rc) {
        LOG_ERR("boot_write_img_confirmed failed: %d", rc);
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
    } else {
        LOG_INF("POST passed — image confirmed");
#ifdef CONFIG_FACTORY_IMAGE_CAPTURE_ON_BOOT
        /* Now that the image is committed, queue a maybe-capture so
         * the very first OTA→confirm cycle on factory-fresh hardware
         * blesses itself as the factory baseline. */
        factory_image_maybe_capture_async();
#endif
    }
#else
    /* Tests stub boot_write_img_confirmed via --wrap. */
    (void)boot_write_img_confirmed();
    LOG_INF("POST passed (test build) — confirm stubbed");
#endif

    smf_set_terminate(SMF_CTX(sm), 1);
}

static void post_failed_timeout_entry(void *obj)
{
    ARG_UNUSED(obj);
    abort_and_reboot(POST_FAILED_TIMEOUT);
}

static void post_failed_cell_entry(void *obj)
{
    ARG_UNUSED(obj);
    abort_and_reboot(POST_FAILED_CELL);
}

static void post_failed_consensus_entry(void *obj)
{
    ARG_UNUSED(obj);
    abort_and_reboot(POST_FAILED_CONSENSUS);
}

static void post_failed_no_ppo2_tx_entry(void *obj)
{
    ARG_UNUSED(obj);
    abort_and_reboot(POST_FAILED_NO_PPO2_TX);
}

static void post_failed_no_handset_entry(void *obj)
{
    ARG_UNUSED(obj);
    abort_and_reboot(POST_FAILED_NO_HANDSET);
}

static void post_failed_solenoid_entry(void *obj)
{
    ARG_UNUSED(obj);
    abort_and_reboot(POST_FAILED_SOLENOID);
}

static const struct smf_state post_states[] = {
    [POST_INIT]              = SMF_CREATE_STATE(NULL,                       NULL,               NULL, NULL, NULL),
    [POST_WAITING_CELLS]     = SMF_CREATE_STATE(post_cells_entry,           post_cells_run,     NULL, NULL, NULL),
    [POST_WAITING_CONSENSUS] = SMF_CREATE_STATE(post_consensus_entry,       post_consensus_run, NULL, NULL, NULL),
    [POST_WAITING_PPO2_TX]   = SMF_CREATE_STATE(post_ppo2_tx_entry,         post_ppo2_tx_run,   NULL, NULL, NULL),
    [POST_WAITING_HANDSET]   = SMF_CREATE_STATE(post_handset_entry,         post_handset_run,   NULL, NULL, NULL),
    [POST_WAITING_SOLENOID]  = SMF_CREATE_STATE(post_solenoid_entry,        post_solenoid_run,  NULL, NULL, NULL),
    [POST_CONFIRMED]         = SMF_CREATE_STATE(post_confirmed_entry,       NULL,               NULL, NULL, NULL),
    [POST_FAILED_TIMEOUT]    = SMF_CREATE_STATE(post_failed_timeout_entry,  NULL,               NULL, NULL, NULL),
    [POST_FAILED_CELL]       = SMF_CREATE_STATE(post_failed_cell_entry,     NULL,               NULL, NULL, NULL),
    [POST_FAILED_CONSENSUS]  = SMF_CREATE_STATE(post_failed_consensus_entry, NULL,              NULL, NULL, NULL),
    [POST_FAILED_NO_PPO2_TX] = SMF_CREATE_STATE(post_failed_no_ppo2_tx_entry, NULL,             NULL, NULL, NULL),
    [POST_FAILED_NO_HANDSET] = SMF_CREATE_STATE(post_failed_no_handset_entry, NULL,             NULL, NULL, NULL),
    [POST_FAILED_SOLENOID]   = SMF_CREATE_STATE(post_failed_solenoid_entry, NULL,               NULL, NULL, NULL),
};

/**
 * @brief Drive the POST state machine to a terminal state.
 *
 * Called both by the K_THREAD_DEFINE'd entry point and by the
 * ZTEST-only sync hook. Initialises the per-run context, transitions
 * into the first WAITING state, and ticks until either POST_CONFIRMED
 * sets the terminate flag or a POST_FAILED_* entry calls sys_reboot.
 */
static void run_post_sequence(void)
{
    PostSmCtx_t sm = {
        .start_ms      = k_uptime_get(),
        .tx_baseline   = divecan_send_get_tx_count(),
        .rx_baseline   = handset_rx_total(),
    };

    smf_set_initial(SMF_CTX(&sm), &post_states[POST_WAITING_CELLS]);
    while (0 == smf_run_state(SMF_CTX(&sm))) {
        k_msleep(POLL_INTERVAL_MS);
    }
}

/* ---- Public API ---- */

PostState_t firmware_confirm_get_state(void)
{
    return (PostState_t)atomic_get(&s_post_state);
}

uint32_t firmware_confirm_get_pass_mask(void)
{
    return (uint32_t)atomic_get(&s_post_pass_mask);
}

#ifdef CONFIG_ZTEST
void firmware_confirm_run_sync_for_test(void)
{
    run_post_sequence();
}

void firmware_confirm_reset_for_test(void)
{
    (void)atomic_set(&s_post_state, (atomic_val_t)POST_INIT);
    (void)atomic_set(&s_post_pass_mask, 0);
}
#endif

/* ---- Thread entry / init ----
 *
 * Production builds wire firmware_confirm_init() from main.c; it
 * inspects the MCUBoot state and either starts the suspended POST
 * thread (a swap was just done, image needs confirm) or leaves it
 * suspended forever (image already confirmed, or a swap is pending
 * for the next boot).
 *
 * Test builds compile this thread out entirely via CONFIG_ZTEST so
 * the suite drives run_post_sequence() inline.
 */

#ifndef CONFIG_ZTEST

static void post_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    run_post_sequence();
}

K_THREAD_DEFINE(firmware_confirm_thread, POST_THREAD_STACK,
                post_thread_entry, NULL, NULL, NULL,
                POST_THREAD_PRIO, 0, K_TICKS_FOREVER);

void firmware_confirm_init(void)
{
    bool confirmed = boot_is_img_confirmed();
    int swap = mcuboot_swap_type();

    if (confirmed) {
        (void)atomic_set(&s_post_state, (atomic_val_t)POST_CONFIRMED);
        LOG_INF("Image already confirmed — POST silent");
    } else if (BOOT_SWAP_TYPE_NONE != swap) {
        /* A swap is queued for the next boot; the unit is still
         * running the OLD image (test cycle hasn't started yet on
         * this boot). Don't run POST on the running image — we'll
         * POST after the next reset. */
        (void)atomic_set(&s_post_state, (atomic_val_t)POST_CONFIRMED);
        LOG_INF("Swap pending — POST deferred to next boot");
    } else {
        LOG_INF("Image not yet confirmed — running POST (%d ms deadline)",
                (int)POST_DEADLINE_MS);
        k_thread_start(firmware_confirm_thread);
    }
}

#else /* CONFIG_ZTEST */

void firmware_confirm_init(void)
{
    /* No-op in tests — the suite calls firmware_confirm_run_sync_for_test()
     * directly when it wants the state machine to run. */
}

#endif
