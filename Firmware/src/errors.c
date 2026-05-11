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

/* ---- zbus error channel ---- */

ZBUS_CHAN_DEFINE(chan_error,
         ErrorEvent_t,
         NULL, NULL,
         ZBUS_OBSERVERS_EMPTY,
         ZBUS_MSG_INIT(.code = OP_ERR_NONE, .detail = 0));

/* ---- Boot-time crash recovery ---- */

static Status_t errors_init(void)
{
    if (CRASH_MAGIC == crash_noinit.magic) {
        /* Take a non-volatile snapshot via memcpy. The volatile qualifier
         * on crash_noinit is only for link-time placement in noinit RAM;
         * by the time we read here (first reader after boot) the data has
         * settled. memcpy avoids the volatile-stripping pointer cast that
         * triggers M23_090/S859/M23_094. */
        (void)memcpy(&last_crash, (const void *)&crash_noinit,
                     sizeof(last_crash));
        had_crash = true;
        crash_noinit.magic = 0U;

        LOG_ERR("Previous crash: reason=%u pc=0x%08x lr=0x%08x cfsr=0x%08x",
            last_crash.reason, last_crash.pc,
            last_crash.lr, last_crash.cfsr);
    }

    return 0;
}

SYS_INIT(errors_init, APPLICATION, 0);

bool errors_get_last_crash(CrashInfo_t *out)
{
    bool valid = false;

    if (had_crash && (out != NULL)) {
        *out = last_crash;
        valid = true;
    }

    return valid;
}

/* ---- Tier 2: MUST_SUCCEED ---- */

FUNC_NORETURN void must_succeed_failed(const char *expr, Status_t rc,
                    const char *file, uint32_t line)
{
    printk("MUST_SUCCEED failed: %s = %d @ %s:%u\n",
           expr, rc, file, line);
    k_oops();
    CODE_UNREACHABLE;
}

/* ---- Tier 3: Operational error publish ---- */

void op_error_publish(OpError_t code, uint32_t detail)
{
    const ErrorEvent_t evt = {
        .code = code,
        .detail = detail,
    };

    (void)zbus_chan_pub(&chan_error, &evt, K_NO_WAIT);
}

/* ---- Tier 4: Fatal operational error ---- */

FUNC_NORETURN void fatal_op_error(FatalOpError_t code, const char *file,
                   uint32_t line)
{
    printk("FATAL OP_ERR %d @ %s:%u\n", (int32_t)code, file, line);

    crash_noinit.magic = CRASH_MAGIC;
    crash_noinit.reason = (uint32_t)code;
    crash_noinit.pc = 0U;
    crash_noinit.lr = 0U;
    crash_noinit.cfsr = 0U;

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
    } else {
        printk("*** FATAL: reason %u  (no ESF) ***\n", reason);
        crash_noinit.pc = 0U;
        crash_noinit.lr = 0U;
    }
#else
    ARG_UNUSED(esf);
    printk("*** FATAL: reason %u ***\n", reason);
    crash_noinit.pc = 0U;
    crash_noinit.lr = 0U;
#endif

    crash_noinit.magic = CRASH_MAGIC;
    crash_noinit.reason = reason;

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
