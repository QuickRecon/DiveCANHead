/**
 * @file main.c
 * @brief Unit tests for the fatal / crash-info paths of errors.c.
 *
 * errors.c installs a strong k_sys_fatal_error_handler() that overrides
 * Zephyr's __weak default, so every fatal path in this binary (k_oops via
 * MUST_SUCCEED, and the direct fatal_op_error() call) funnels through it and
 * ends in sys_reboot(). The CMake link wraps sys_reboot(); the wrap either
 * longjmp()s back to the test body (for calls made directly on the main test
 * thread) or aborts the calling thread (for the k_oops() path, where
 * longjmp()ing out of the kernel fatal machinery on native_sim is unsafe —
 * see CLAUDE.md).
 *
 * Not reachable in-process (documented, not tested):
 *   - errors_init()'s crash-replay body (lines that copy crash_noinit ->
 *     last_crash) needs a CRASH_MAGIC record surviving a warm reset in noinit
 *     RAM; a single host process boots exactly once with that slot zeroed.
 *   - errors_get_last_crash()'s had_crash==true arm depends on that same
 *     boot-time replay, so it is unreachable here too.
 * Both are flagged in the final report for the Phase-5 GCOVR_EXCL list.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>

#include <setjmp.h>

#include "errors.h"

/* ---- sys_reboot() wrap: longjmp escape + thread-abort fallback ---- */

static struct {
    uint32_t calls;
    bool jmp_armed;
    jmp_buf jmp;
} reboot_hook;

FUNC_NORETURN void __wrap_sys_reboot(int type);
FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    ARG_UNUSED(type);
    ++reboot_hook.calls;
    if (reboot_hook.jmp_armed) {
        reboot_hook.jmp_armed = false;
        longjmp(reboot_hook.jmp, 1);
    }
    /* Reached from the k_oops() fatal path on a sacrificial worker: unwind by
     * aborting the current thread rather than longjmp()ing through the kernel
     * fatal machinery. */
    k_thread_abort(k_current_get());
    CODE_UNREACHABLE;
}

/* ---- zbus reject validator: forces zbus_chan_pub() to return -ENOMSG ---- */

static bool reject_validator(const void *msg, size_t msg_size)
{
    ARG_UNUSED(msg);
    ARG_UNUSED(msg_size);
    return false;
}

ZBUS_CHAN_DEFINE(chan_test_reject,
                 uint32_t,
                 reject_validator,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0));

/* ---- MUST_SUCCEED worker: k_oops() never returns, so run it detached ---- */

#define MUST_SUCCEED_WORKER_STACK 2048
static K_THREAD_STACK_DEFINE(worker_stack, MUST_SUCCEED_WORKER_STACK);
static struct k_thread worker;

#define WORKER_PRIO 5
static const int32_t WORKER_JOIN_TIMEOUT_MS = 5000;
static const uint32_t ZBUS_PUB_TIMEOUT_MS = 100U;
static const Status_t MUST_SUCCEED_BAD_RC = -EIO;

static void must_succeed_worker_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    /* Fails (rc != 0) -> must_succeed_failed() -> k_oops() -> fatal handler
     * -> sys_reboot() wrap -> k_thread_abort(this thread). */
    MUST_SUCCEED(MUST_SUCCEED_BAD_RC);
    CODE_UNREACHABLE;
}

ZTEST_SUITE(errors_fatal, NULL, NULL, NULL, NULL, NULL);

/* ---- Tier 1: crash-info accessor ---- */

/** @brief errors_get_last_crash() reports no crash on a clean (single) boot. */
ZTEST(errors_fatal, test_get_last_crash_clean_boot)
{
    CrashInfo_t info = {0};

    /* No warm-reset crash record exists in this process, so both the NULL and
     * the valid-pointer calls take the had_crash==false arm and report false. */
    zassert_false(errors_get_last_crash(&info),
                  "clean boot must report no prior crash");
    zassert_false(errors_get_last_crash(NULL),
                  "NULL out must be handled without a crash record");
}

/* ---- Tier 3: checked publish ---- */

/** @brief zbus_pub_checked() success arm: a valid channel publishes cleanly. */
ZTEST(errors_fatal, test_zbus_pub_checked_success)
{
    const ErrorEvent_t evt = {.code = OP_ERR_NONE, .detail = 0U};

    /* chan_error accepts any ErrorEvent_t (NULL validator) -> ret == 0, so the
     * error arm is skipped. */
    zbus_pub_checked(&chan_error, &evt, K_MSEC(ZBUS_PUB_TIMEOUT_MS));
}

/** @brief zbus_pub_checked() error arm: a rejecting validator drives OP_ERR_QUEUE. */
ZTEST(errors_fatal, test_zbus_pub_checked_failure_raises_op_error)
{
    const uint32_t payload = 0U;

    /* reject_validator returns false, so zbus_chan_pub() returns -ENOMSG and
     * zbus_pub_checked() takes the OP_ERROR_DETAIL(OP_ERR_QUEUE, ...) arm.
     * op_error_publish() then posts on chan_error with K_NO_WAIT. */
    zbus_pub_checked(&chan_test_reject, &payload, K_MSEC(ZBUS_PUB_TIMEOUT_MS));
}

/* ---- Tier 4: fatal_op_error() ---- */

/** @brief fatal_op_error() persists a crash record and reboots (never returns). */
ZTEST(errors_fatal, test_fatal_op_error_reboots)
{
    uint32_t before = reboot_hook.calls;

    reboot_hook.jmp_armed = true;
    if (0 == setjmp(reboot_hook.jmp)) {
        FATAL_OP_ERROR(FATAL_CRITICAL);
        zassert_unreachable("fatal_op_error() must not return");
    }
    reboot_hook.jmp_armed = false;

    zassert_equal(reboot_hook.calls, before + 1U,
                  "fatal_op_error() must call sys_reboot() exactly once");
}

/** @brief Every FatalOpError_t code drives the same persist-then-reboot path. */
ZTEST(errors_fatal, test_fatal_op_error_all_codes)
{
    const FatalOpError_t codes[] = {
        FATAL_BUFFER_OVERRUN,
        FATAL_UNDEFINED_STATE,
        FATAL_UNDEFINED_CAL,
        FATAL_FS,
    };

    for (uint8_t i = 0U; i < (uint8_t)(sizeof(codes) / sizeof(codes[0])); ++i) {
        uint32_t before = reboot_hook.calls;

        reboot_hook.jmp_armed = true;
        if (0 == setjmp(reboot_hook.jmp)) {
            fatal_op_error(codes[i], __FILE__, __LINE__);
            zassert_unreachable("fatal_op_error() must not return");
        }
        reboot_hook.jmp_armed = false;

        zassert_equal(reboot_hook.calls, before + 1U,
                      "each fatal code must reboot exactly once");
    }
}

/* ---- Zephyr fatal handler override ---- */

/** @brief k_sys_fatal_error_handler() records context and reboots (ESF absent). */
ZTEST(errors_fatal, test_sys_fatal_handler_no_esf)
{
    uint32_t before = reboot_hook.calls;

    reboot_hook.jmp_armed = true;
    if (0 == setjmp(reboot_hook.jmp)) {
        /* native_sim is not CONFIG_ARM, so the handler takes its non-ARM arm;
         * esf is NULL there regardless. */
        k_sys_fatal_error_handler(K_ERR_KERNEL_OOPS, NULL);
        zassert_unreachable("fatal handler must not return");
    }
    reboot_hook.jmp_armed = false;

    zassert_equal(reboot_hook.calls, before + 1U,
                  "the fatal handler must call sys_reboot()");
}

/** @brief MUST_SUCCEED() on a failing rc oopses through the fatal handler. */
ZTEST(errors_fatal, test_must_succeed_failure_oops)
{
    uint32_t before = reboot_hook.calls;

    /* k_oops() -> fatal handler -> sys_reboot() wrap aborts the worker (jmp is
     * disarmed, so the wrap takes the thread-abort branch). */
    reboot_hook.jmp_armed = false;
    (void)k_thread_create(&worker, worker_stack,
                          K_THREAD_STACK_SIZEOF(worker_stack),
                          must_succeed_worker_fn, NULL, NULL, NULL,
                          K_PRIO_PREEMPT(WORKER_PRIO), 0, K_NO_WAIT);

    zassert_ok(k_thread_join(&worker, K_MSEC(WORKER_JOIN_TIMEOUT_MS)),
               "must_succeed worker did not terminate");
    zassert_equal(reboot_hook.calls, before + 1U,
                  "MUST_SUCCEED failure must route through sys_reboot()");
}
