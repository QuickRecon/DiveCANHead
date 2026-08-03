/**
 * @file calibration_store.h
 * @brief Persistence layer for oxygen-cell calibration coefficients.
 *
 * Owns the "cal" settings subtree: the in-memory coefficient cache, the
 * settings handler that backs it, per-cell save/load, and the boot-time load
 * that restores stored coefficients into the cache. Split out of
 * calibration.c so the cell drivers (analog/diveo2/o2s) can depend on just
 * the store without pulling in the calibration state machine, cal thread, or
 * solenoid/divecan channels.
 */
#ifndef CALIBRATION_STORE_H
#define CALIBRATION_STORE_H

#include "common.h"            /* Status_t */
#include "oxygen_cell_types.h" /* CalCoeff_t */

#include <stdint.h>

/**
 * @brief Populate the calibration-coefficient cache from the "cal" NVS subtree.
 *
 * The Zephyr settings subsystem does not auto-replay stored values, and no
 * boot-time settings_load() covers the "cal" subtree (main() only loads "rt").
 * Without this call the settings handler's set() callback never runs on a
 * fresh boot, so the coefficient cache stays zero-initialised and every cell's
 * *_load_cal() reads a default coefficient — the saved calibration is present
 * in flash but never restored, so it appears "not to persist" across reboots.
 *
 * Cell drivers call this from their *_load_cal() before reading the cache.
 * Load-once and thread-safe: the first caller performs the NVS read while any
 * concurrent caller blocks until it completes, so no cell ever observes a
 * half-populated cache and the flash cost is paid exactly once. Safe to call
 * before the settings subsystem has otherwise been initialised (it runs
 * settings_subsys_init() itself, idempotently).
 */
void calibration_load_coefficients(void);

/**
 * @brief Persist a calibration coefficient to non-volatile settings storage.
 *
 * Writes the "cal/cellN" key then forces a read-back reload of the "cal"
 * subtree so the in-memory cache reflects what actually reached flash.
 *
 * @param cell_num Zero-based cell index (0–2).
 * @param coeff    Calibration coefficient to store.
 * @return 0 on success, negative errno on settings write failure.
 */
Status_t calibration_store_save(uint8_t cell_num, CalCoeff_t coeff);

/**
 * @brief Load a calibration coefficient from the settings cache.
 *
 * @param cell_num Zero-based cell index (0–2).
 * @param coeff    Output pointer; written with the loaded coefficient on success.
 * @return 0 on success, -ENOENT if the key is absent or the stored size does
 *         not match.
 */
Status_t calibration_store_load(uint8_t cell_num, CalCoeff_t *coeff);

#ifdef CONFIG_ZTEST
/**
 * @brief Test-only seam: seed a cached coefficient directly (bypasses NVS).
 *
 * Lets focused cell-thread unit tests exercise the load path without a flash
 * backend — set the cache, then let *_load_cal() read it through the real
 * settings handler. Only compiled under CONFIG_ZTEST so production builds
 * can't reach the cache through a side door.
 *
 * @param cell_num Zero-based cell index (0–2).
 * @param coeff    Coefficient to place in the cache slot.
 */
void calibration_store_seed_cached(uint8_t cell_num, CalCoeff_t coeff);
#endif

#endif /* CALIBRATION_STORE_H */
