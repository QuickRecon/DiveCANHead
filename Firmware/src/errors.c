#include "errors.h"

#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(errors, LOG_LEVEL_ERR);

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

static int errors_init(void)
{
	if (crash_noinit.magic == CRASH_MAGIC) {
		last_crash = *(const CrashInfo_t *)&crash_noinit;
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

FUNC_NORETURN void must_succeed_failed(const char *expr, int rc,
					const char *file, unsigned int line)
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
				   unsigned int line)
{
	printk("FATAL OP_ERR %d @ %s:%u\n", (int)code, file, line);

	crash_noinit.magic = CRASH_MAGIC;
	crash_noinit.reason = (uint32_t)code;
	crash_noinit.pc = 0U;
	crash_noinit.lr = 0U;
	crash_noinit.cfsr = 0U;

	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}

/* ---- Zephyr fatal error handler override ----
 *
 * Overrides the __weak default in Zephyr. All fatal paths (k_oops, k_panic,
 * __ASSERT, CPU exceptions, stack canary) route here.
 * We persist crash context to noinit RAM and reboot.
 */

void k_sys_fatal_error_handler(unsigned int reason,
			       const struct arch_esf *esf)
{
	printk("*** FATAL: reason %u ***\n", reason);

	crash_noinit.magic = CRASH_MAGIC;
	crash_noinit.reason = reason;

#if defined(CONFIG_ARM)
	if (esf != NULL) {
		crash_noinit.pc = esf->basic.pc;
		crash_noinit.lr = esf->basic.lr;
	} else {
		crash_noinit.pc = 0U;
		crash_noinit.lr = 0U;
	}
#else
	ARG_UNUSED(esf);
	crash_noinit.pc = 0U;
	crash_noinit.lr = 0U;
#endif

#if defined(CONFIG_CPU_CORTEX_M)
	crash_noinit.cfsr = SCB->CFSR;
#else
	crash_noinit.cfsr = 0U;
#endif

	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}
