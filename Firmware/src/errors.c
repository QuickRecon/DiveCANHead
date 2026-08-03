/**
 * @file errors.c
 * @brief Error handling infrastructure — crash persistence, fatal reboot, and op-error channel
 *
 * Implements a four-tier error model:
 *   1. __ASSERT — programming invariants (debug only)
 *   2. MUST_SUCCEED — init-time calls that must not fail (triggers k_oops)
 *   3. OP_ERROR — non-fatal runtime errors published on chan_error
 *   4. FATAL_OP_ERROR — unrecoverable conditions that persist crash info and reboot
 *
 * Crash info (PC, LR, CFSR, reason) is stored in noinit RAM so it survives a
 * warm reset and can be retrieved via errors_get_last_crash() at the next boot.
 * The Zephyr fatal error handler is overridden here to route all fault paths
 * (k_oops, k_panic, CPU exceptions, stack canary) through the same mechanism.
 */

#include "errors.h"

#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/zbus/zbus.h>

#include <string.h>

#include "common.h"

LOG_MODULE_REGISTER(errors, LOG_LEVEL_ERR);

/* RTT-drain spin count tuned empirically — at 64 MHz this is ~10–20 ms,
 * enough for the SEGGER J-Link backend to flush the last RTT message
 * before the cold reboot wipes the buffer. */
#define RTT_DRAIN_SPIN_COUNT 1000000

/* ---- Crash info in noinit RAM (survives warm resets) ---- */

static volatile CrashInfo_t crash_noinit __noinit;

/* Copy taken at boot so the noinit slot can be cleared immediately */
static CrashInfo_t last_crash;
static bool had_crash;

#if defined(CONFIG_ARM)
#define XPSR_IPSR_MASK 0x1FFU
#define XPSR_STACK_ALIGN_FLAG 0x200U

static uint32_t fault_sp_from_esf(const struct arch_esf *esf)
{
    uint32_t sp = (uint32_t)(uintptr_t)esf + sizeof(esf->basic);

    if (0U != (esf->basic.xpsr & XPSR_STACK_ALIGN_FLAG)) {
        sp += sizeof(uint32_t);
    }
    return sp;
}
#endif

/* ---- zbus error channel ---- */

ZBUS_CHAN_DEFINE(chan_error,
         ErrorEvent_t,
         NULL, NULL,
         ZBUS_OBSERVERS_EMPTY,
         ZBUS_MSG_INIT(.code = OP_ERR_NONE, .detail = 0));

/* ---- Boot-time crash recovery ---- */

/**
 * @brief SYS_INIT callback — snapshot crash info left in noinit RAM
 *
 * If the noinit magic value is present this function copies the crash record
 * to a normal RAM snapshot and logs the crash reason. The noinit magic remains
 * set until boot_history persists the record and acknowledges it, allowing a
 * failed startup flash write to retry on the next boot.
 * The snapshot is then accessible via errors_get_last_crash() for the rest of
 * this boot cycle.
 *
 * @return Always 0 (failure here is non-fatal; the system should still boot)
 */
static Status_t errors_init(void)
{
    /* GCOVR_EXCL_START — crash-replay path: needs CRASH_MAGIC to survive a
     * warm reset in noinit RAM. A native_sim process boots exactly once
     * with the slot zeroed, so only hardware can take this arm. */
    if (CRASH_MAGIC == crash_noinit.magic) {
        /* Take a non-volatile snapshot via memcpy. The volatile qualifier
         * on crash_noinit is only for link-time placement in noinit RAM;
         * by the time we read here (first reader after boot) the data has
         * settled. memcpy's `const void *src` parameter has no volatile
         * overload in C, so reading a volatile source through it still
         * requires dropping the qualifier at the call boundary (the
         * M23_090/S859 cast below) — but that is the standard, minimal-UB
         * way to bulk-copy a volatile buffer, safer than aliasing it
         * through a typed non-volatile pointer. Accepted per-issue on
         * SonarCloud (see docs/SONARQUBE_ACCEPTED_ISSUES.md). */
        (void)memcpy(&last_crash, (const void *)&crash_noinit,
                     sizeof(last_crash));
        had_crash = true;

        LOG_ERR("Previous crash: reason=%u pc=0x%08x lr=0x%08x cfsr=0x%08x thread=0x%08x",
            last_crash.reason, last_crash.pc,
            last_crash.lr, last_crash.cfsr, last_crash.thread);
    }
    /* GCOVR_EXCL_STOP */

    return 0;
}

SYS_INIT(errors_init, APPLICATION, 0);

/**
 * @brief Retrieve crash info recorded during the previous boot cycle
 *
 * @param out Populated with the saved crash record when a prior crash exists;
 *            must not be NULL
 * @return true if a prior crash was detected and *out was written
 */
bool errors_get_last_crash(CrashInfo_t *out)
{
    bool valid = false;

    if (had_crash && (out != NULL)) {
        /* GCOVR_EXCL_START — only reachable when the crash-replay arm in
         * errors_init() ran, which needs noinit RAM surviving a warm
         * reset (hardware only; see exclusion above). */
        *out = last_crash;
        valid = true;
        /* GCOVR_EXCL_STOP */
    }

    return valid;
}

void errors_acknowledge_last_crash(void)
{
    if (had_crash) {
        crash_noinit.magic = 0U;
    }
}

/* ---- Tier 2: MUST_SUCCEED ---- */

/**
 * @brief Called when a MUST_SUCCEED() assertion fails — logs and triggers k_oops
 *
 * @param expr String representation of the failing expression
 * @param rc   Return code that caused the failure
 * @param file Source file name (from __FILE__)
 * @param line Line number (from __LINE__)
 */
FUNC_NORETURN void must_succeed_failed(const char *expr, Status_t rc,
                    const char *file, uint32_t line)
{
    (void)printk("MUST_SUCCEED failed: %s = %d @ %s:%u\n",
           expr, rc, file, line);
    k_oops();
    CODE_UNREACHABLE;
}

/* ---- Tier 3: Operational error publish ---- */

/**
 * @brief Publish a non-fatal operational error to chan_error
 *
 * Uses K_NO_WAIT — if the channel is busy the event is silently dropped
 * rather than blocking the caller. This MUST stay non-blocking: op errors are
 * raised from arbitrary contexts (including inside other zbus operations), so a
 * bounded/blocking publish here could deadlock or stall a critical path. A
 * dropped event is acceptable because chan_error is best-effort telemetry — the
 * durable error record is the noinit-RAM histogram (error_histogram / DID
 * 0xF260), not this channel.
 *
 * @param code   Error code identifying the fault condition
 * @param detail Optional numeric detail (e.g. peripheral address, status register)
 */
void op_error_publish(OpError_t code, uint32_t detail)
{
    const ErrorEvent_t evt = {
        .code = code,
        .detail = detail,
    };

    (void)zbus_chan_pub(&chan_error, &evt, K_NO_WAIT);
}

void zbus_pub_checked(const struct zbus_channel *chan, const void *msg,
                      k_timeout_t timeout)
{
    Status_t ret = zbus_chan_pub(chan, msg, timeout);

    if (0 != ret) {
        OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-ret));
    }
}

/* ---- Tier 4: Fatal operational error ---- */

/**
 * @brief Record an unrecoverable operational error to noinit RAM and reboot
 *
 * Writes a crash record (with code as the reason field) to noinit RAM so it
 * survives the cold reboot, spins briefly to drain the RTT buffer, then calls
 * sys_reboot().  Never returns.
 *
 * @param code Fatal error code classifying the condition
 * @param file Source file name (from __FILE__)
 * @param line Line number (from __LINE__)
 */
FUNC_NORETURN void fatal_op_error(FatalOpError_t code, const char *file,
                   uint32_t line)
{
    (void)printk("FATAL OP_ERR %d @ %s:%u\n", (int32_t)code, file, line);

    crash_noinit.magic = CRASH_MAGIC;
    crash_noinit.reason = (uint32_t)code;
    crash_noinit.pc = 0U;
    crash_noinit.lr = 0U;
    crash_noinit.cfsr = 0U;
    crash_noinit.sp = 0U;
    crash_noinit.xpsr = 0U;
    crash_noinit.exc_return = 0U;
    crash_noinit.stack_source = CRASH_STACK_SOURCE_UNKNOWN;
    crash_noinit.thread = (uint32_t)(uintptr_t)k_current_get();

    /* Don't call LOG_PANIC() here — the RTT log backend's flush path
     * uses k_busy_wait which touches the systick spinlock. If we're
     * already in a fault context that spinlock may be held, causing a
     * nested assert. printk goes directly to RTT without the logging
     * subsystem so it's safe in any context. The spin delay lets the
     * RTT buffer drain over SWD before we reboot. The loop is volatile
     * to defeat compiler optimisation of the empty body — this is a
     * busy-wait, not inter-thread synchronisation, so an atomic_t is
     * not appropriate here (S3687 false positive). */
    for (volatile Status_t i = 0; i < RTT_DRAIN_SPIN_COUNT; ++i) {
        /* Intentionally empty — see comment above for the busy-wait rationale. */
    }

    sys_reboot(SYS_REBOOT_COLD);
    CODE_UNREACHABLE;
}

/* ---- Zephyr fatal error handler override ----
 *
 * Overrides the __weak default in Zephyr. All fatal paths (k_oops, k_panic,
 * __ASSERT, CPU exceptions, stack canary) route here.
 * We persist crash context to noinit RAM and reboot.
 */

/**
 * @brief Zephyr fatal error handler override — persists crash context and reboots
 *
 * Overrides the __weak default.  All fatal paths (k_oops, k_panic, __ASSERT,
 * CPU exceptions, stack canary) route here.  Crash context (reason, PC, LR,
 * CFSR) is written to noinit RAM before the cold reboot so it can be retrieved
 * on the next boot via errors_get_last_crash().
 *
 * @param reason Zephyr fatal reason code (K_ERR_*)
 * @param esf    Exception stack frame; may be NULL for software-triggered faults
 */
void k_sys_fatal_error_handler(uint32_t reason,
                   const struct arch_esf *esf)
{
    /* Print the crash info directly to RTT via printk (not LOG, which
     * can crash in fault context due to systick spinlock contention) */
#if defined(CONFIG_ARM)
    if (esf != NULL) {
        printk("*** FATAL: reason %u  pc=0x%08x  lr=0x%08x ***\n",
               reason, esf->basic.pc, esf->basic.lr);
        crash_noinit.pc = esf->basic.pc;
        crash_noinit.lr = esf->basic.lr;
        crash_noinit.xpsr = esf->basic.xpsr;
#if defined(CONFIG_EXTRA_EXCEPTION_INFO)
        crash_noinit.exc_return = esf->extra_info.exc_return;
        if (0U != (esf->basic.xpsr & XPSR_IPSR_MASK)) {
            crash_noinit.sp = esf->extra_info.msp;
            crash_noinit.stack_source = CRASH_STACK_SOURCE_MSP;
        } else if (esf->extra_info.callee != NULL) {
            crash_noinit.sp = esf->extra_info.callee->psp;
            crash_noinit.stack_source = CRASH_STACK_SOURCE_PSP;
        } else {
            crash_noinit.sp = esf->extra_info.msp;
            crash_noinit.stack_source = CRASH_STACK_SOURCE_MSP;
        }
#else
        crash_noinit.sp = fault_sp_from_esf(esf);
        crash_noinit.exc_return = 0U;
        crash_noinit.stack_source =
            (0U != (esf->basic.xpsr & XPSR_IPSR_MASK)) ?
            CRASH_STACK_SOURCE_MSP : CRASH_STACK_SOURCE_PSP;
#endif
    } else {
        crash_noinit.pc = 0U;
        crash_noinit.lr = 0U;
        crash_noinit.sp = 0U;
        /* No ESF (e.g. K_ERR_SPURIOUS_IRQ from z_irq_spurious): we are still
         * executing in the offending exception's context, so IPSR identifies
         * the vector that fired (external IRQn = IPSR - 16). Without this the
         * record is all-zeros and the crash is undebuggable — bit us in the
         * 2026-08-01 HIL release run (17 SPURIOUS_IRQ records, no vector). */
#if defined(CONFIG_CPU_CORTEX_M)
        crash_noinit.xpsr = __get_IPSR();
#else
        crash_noinit.xpsr = 0U;
#endif
        crash_noinit.exc_return = 0U;
        crash_noinit.stack_source = CRASH_STACK_SOURCE_UNKNOWN;
        (void)printk("*** FATAL: reason %u  (no ESF) ipsr=%u ***\n", reason,
                 crash_noinit.xpsr);
    }
#else
    ARG_UNUSED(esf);
    (void)printk("*** FATAL: reason %u ***\n", reason);
    crash_noinit.pc = 0U;
    crash_noinit.lr = 0U;
    crash_noinit.sp = 0U;
    crash_noinit.xpsr = 0U;
    crash_noinit.exc_return = 0U;
    crash_noinit.stack_source = CRASH_STACK_SOURCE_UNKNOWN;
#endif

    crash_noinit.magic = CRASH_MAGIC;
    crash_noinit.reason = reason;
    /* Record the faulting thread so a stack overflow (K_ERR_STACK_CHK_FAIL=2)
     * can be attributed to a specific thread on the next boot. */
    crash_noinit.thread = (uint32_t)(uintptr_t)k_current_get();
    (void)printk("*** FATAL thread=%s (%p) ***\n",
           k_thread_name_get(k_current_get()), (void *)k_current_get());

#if defined(CONFIG_CPU_CORTEX_M)
    crash_noinit.cfsr = SCB->CFSR;
#endif

    /* Spin briefly to let RTT drain the fatal message before reboot.
     * See fatal_op_error() above for the volatile / S3687 rationale. */
    for (volatile Status_t i = 0; i < RTT_DRAIN_SPIN_COUNT; ++i) {
        /* Intentionally empty — busy-wait for RTT drain. */
    }

    sys_reboot(SYS_REBOOT_COLD);
    CODE_UNREACHABLE;
}
