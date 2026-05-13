/**
 * @file error_histogram.h
 * @brief Per-error-code occurrence counters persisted to NVS.
 *
 * A passive zbus subscriber on `chan_error` increments an in-RAM atomic
 * counter for every published OpError_t code.  A low-frequency timer
 * flushes the histogram into the Zephyr settings subsystem (NVS backend)
 * so post-dive analytics can read accumulated error rates that survive
 * a power cycle.  Exposed externally via UDS DID 0xF260.
 *
 * The legacy STM32 firmware declared a similar API
 * (`flash.c::Get/SetNonFatalError`) but never implemented it; this is a
 * net-new capability in the Zephyr port.
 */
#ifndef ERROR_HISTOGRAM_H
#define ERROR_HISTOGRAM_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/** @brief Number of distinct error codes tracked (one per OpError_t value). */
#define ERROR_HISTOGRAM_COUNT  ((size_t)OP_ERR_MAX)

/** @brief Byte length of the histogram when serialised as uint16 LE counts. */
#define ERROR_HISTOGRAM_BYTES  (ERROR_HISTOGRAM_COUNT * sizeof(uint16_t))

/**
 * @brief Initialise the histogram subsystem.
 *
 * Loads any previously persisted histogram from NVS into the in-RAM
 * counters and starts the periodic save timer.  Must be called from main()
 * after the settings subsystem has been initialised (i.e. after
 * `ppo2_control_init()` / `runtime_settings_load()`).
 */
void error_histogram_init(void);

/**
 * @brief Copy the live histogram into a destination buffer as uint16 counts.
 *
 * Each slot saturates at `0xFFFF` — a code that has fired more times than
 * that will read back as `UINT16_MAX`.  Safe to call from any thread.
 *
 * @param out       Destination buffer; must hold at least ERROR_HISTOGRAM_COUNT u16 entries.
 * @param out_count Number of u16 slots available in @p out.
 * @return Number of bytes written to @p out, or 0 on null / short buffer.
 */
size_t error_histogram_snapshot(uint16_t *out, size_t out_count);

/**
 * @brief Reset all per-error counters to zero and persist immediately.
 *
 * Used by the UDS clear DID (0xF261) and by service tooling that wants a
 * fresh histogram for a single dive.  Performs an immediate NVS write so
 * the cleared state survives a power cycle even if the next periodic save
 * tick has not yet fired.
 *
 * @return 0 on success, or a negative errno from the underlying NVS write.
 */
int error_histogram_clear(void);

#endif /* ERROR_HISTOGRAM_H */
