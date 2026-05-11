#ifndef ERRORS_H
#define ERRORS_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

/* ---- Crash info (persisted in noinit RAM across warm resets) ---- */

#define CRASH_MAGIC 0xDEADC0DEU

typedef struct {
    uint32_t magic;
    uint32_t reason;    /* K_ERR_* value, or FatalOpError_t for Tier 4 */
    uint32_t pc;
    uint32_t lr;
    uint32_t cfsr;      /* Cortex-M Configurable Fault Status Register */
} CrashInfo_t;

/**
 * Read crash info from the previous boot.
 * Returns true and populates *out if a crash was recorded.
 * Valid for the entire session — data is copied at boot before noinit is cleared.
 */
bool errors_get_last_crash(CrashInfo_t *out);

/* ---- Tier 2: MUST_SUCCEED — init-time fatal check ----
 *
 * For kernel/driver API calls that return int and must return 0.
 * If the call fails, logs the expression + return code + location,
 * then triggers k_oops() which routes through our fatal handler → reboot.
 * Never compiles out.
 */

FUNC_NORETURN void must_succeed_failed(const char *expr, int rc,
                    const char *file, unsigned int line);

#define MUST_SUCCEED(expr) do {                    \
    int _must_rc = (expr);                     \
    if (_must_rc != 0) {                       \
        must_succeed_failed(#expr, _must_rc, \
                    __FILE__, __LINE__); \
    }                                          \
} while (false)

/* ---- Tier 3: Non-fatal operational errors ----
 *
 * Recoverable conditions that are logged and published to zbus for
 * subscribers (DiveCAN status composer, flash logger, etc.) but allow
 * the system to continue operating with graceful degradation.
 */

typedef enum {
    OP_ERR_NONE = 0,

    /* ---- Hardware ---- */

    /** We failed to undertake an I2C operation */
    OP_ERR_I2C_BUS,

    /** We failed to undertake a UART operation */
    OP_ERR_UART,

    /** We couldn't add the CAN message to the outbound buffer */
    OP_ERR_CAN_TX,

    /** Inbound CAN message is longer than 8 bytes */
    OP_ERR_CAN_OVERFLOW,

    /** Error occurred when trying to read the internal ADC */
    OP_ERR_INT_ADC,

    /** Error occurred when reading the external ADC (ADS1115) */
    OP_ERR_EXT_ADC,

    /** We weren't able to read/write flash storage */
    OP_ERR_FLASH,

    /* ---- Sensors ---- */

    /** A cell has reported a value that we can't display */
    OP_ERR_CELL_OVERRANGE,

    /** A cell has reported an error */
    OP_ERR_CELL_FAILURE,

    /** The cell number can't be mapped to an input (too high?) */
    OP_ERR_INVALID_CELL,

    /* ---- Math / Safety ---- */

    /** A computation produced an out-of-range or overflow result */
    OP_ERR_MATH,

    /* ---- Calibration ---- */

    /** The configured calibration method cannot complete */
    OP_ERR_CAL_METHOD,

    /** The calibration info we stored is not the calibration info we got */
    OP_ERR_CAL_MISMATCH,

    /* ---- Power ---- */

    /** VBus is undervolted, cell readings are unreliable */
    OP_ERR_VBUS_UNDERVOLT,

    /** VCC is undervolted, can't write to flash */
    OP_ERR_VCC_UNDERVOLT,

    /** We tried to fire the solenoid but were inhibited */
    OP_ERR_SOLENOID_DISABLED,

    /* ---- ISO-TP transport ---- */

    /** ISO-TP timeout waiting for flow control or consecutive frame */
    OP_ERR_ISOTP_TIMEOUT,

    /** ISO-TP consecutive frame sequence number error */
    OP_ERR_ISOTP_SEQ,

    /** ISO-TP message exceeds maximum payload */
    OP_ERR_ISOTP_OVERFLOW,

    /** ISO-TP invalid state transition */
    OP_ERR_ISOTP_STATE,

    /* ---- UDS ---- */

    /** UDS sent a negative response — log NRC for debugging */
    OP_ERR_UDS_NRC,

    /** UDS response buffer too full to fit data */
    OP_ERR_UDS_TOO_FULL,

    /** An invalid UDS operation was attempted */
    OP_ERR_UDS_INVALID,

    /* ---- Configuration ---- */

    /** Error occurred when trying to load the config */
    OP_ERR_CONFIG,

    /* ---- System ---- */

    /** What we were waiting for never came */
    OP_ERR_TIMEOUT,

    /** The data we are looking at is out of date */
    OP_ERR_OUT_OF_DATE,

    /** We weren't able to lodge an element in the queue */
    OP_ERR_QUEUE,

    /** A null pointer was passed to a function not designed to handle it */
    OP_ERR_NULL_PTR,

    /** Logging quit due to an error */
    OP_ERR_LOGGING,

    /** Log push queue full — oldest message dropped */
    OP_ERR_LOG_TRUNCATED,

    /** Check code that should be unreachable — something strange happened */
    OP_ERR_UNREACHABLE,

    /** We encountered an error we don't know how to handle */
    OP_ERR_UNKNOWN,

    OP_ERR_MAX
} OpError_t;

typedef struct {
    OpError_t code;
    uint32_t detail;
} ErrorEvent_t;

ZBUS_CHAN_DECLARE(chan_error);

/**
 * Publish an operational error to the zbus error channel.
 * Prefer the OP_ERROR / OP_ERROR_DETAIL macros — they also emit a LOG_ERR
 * tagged with the caller's log module.
 */
void op_error_publish(OpError_t code, uint32_t detail);

/*
 * OP_ERROR / OP_ERROR_DETAIL — report a non-fatal operational error.
 *
 * Logs via the caller's LOG module (so the log line shows the originating
 * subsystem, not "errors") and publishes to chan_error for subscribers
 * (DiveCAN status composer, flash logger, etc.).
 *
 * Requires LOG_MODULE_REGISTER or LOG_MODULE_DECLARE in the calling TU.
 */
#define OP_ERROR(code) do {                                      \
    LOG_ERR("ERR %d", (int)(code));                          \
    op_error_publish((code), 0U);                            \
} while (false)

#define OP_ERROR_DETAIL(code, detail) do {                             \
    LOG_ERR("ERR %d (0x%x)", (int)(code), (unsigned int)(detail)); \
    op_error_publish((code), (uint32_t)(detail));                  \
} while (false)

/* ---- Tier 4: Fatal operational errors ----
 *
 * Unrecoverable runtime conditions (not programming bugs — those are
 * __ASSERT). Persists the error code to noinit RAM and reboots.
 *
 * This is a separate enum from OpError_t so the compiler catches accidental
 * use of a non-fatal code in a fatal call (or vice versa).
 */

typedef enum {
    /** Ran past the end of a buffer — even if it didn't hard fault,
     *  we've clobbered unknown memory in an unknown way */
    FATAL_BUFFER_OVERRUN = 0,

    /** Super-duper UNREACHABLE — we don't know if it's safe to continue */
    FATAL_UNDEFINED_STATE,

    /** Configured calibration method is not defined (config corrupt?) */
    FATAL_UNDEFINED_CAL,

    /** Filesystem corruption or unrecoverable I/O failure */
    FATAL_FS,

    /** RTOS critical error handler triggered */
    FATAL_CRITICAL,

    FATAL_OP_MAX
} FatalOpError_t;

FUNC_NORETURN void fatal_op_error(FatalOpError_t code, const char *file,
                   unsigned int line);

#define FATAL_OP_ERROR(code) \
    fatal_op_error((code), __FILE__, __LINE__)

#endif /* ERRORS_H */
