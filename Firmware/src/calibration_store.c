/**
 * @file calibration_store.c
 * @brief Persistence layer for oxygen-cell calibration coefficients.
 *
 * Owns the "cal" settings subtree: the in-memory coefficient cache, the
 * settings handler that backs it, per-cell save/load, and the boot-time
 * load-once that restores stored coefficients into the cache. Split out of
 * calibration.c so the cell drivers can depend on the store alone, without the
 * calibration state machine, cal thread, or solenoid/divecan channels.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#include "calibration_store.h"
#include "oxygen_cell_types.h"
#include "errors.h"
#include "common.h"
#include "external_flash.h"

#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(calibration_store, LOG_LEVEL_INF);

/* ---- Named constants ---- */

#define CAL_SETTINGS_KEY     "cal/cell"
#define CAL_SETTINGS_SUBTREE "cal"
#define CAL_KEY_BUF_LEN      16U  /* settings key string buffer length */
#define CAL_KEY_PREFIX_LEN   4U   /* strlen("cell") in a "cal/cellN" settings key */

/**
 * @brief Return a pointer to the module-static in-memory calibration cache.
 *
 * Backs the "cal" settings subtree. Populated by the settings handlers below
 * whenever the "cal" subtree is loaded: at boot via
 * calibration_load_coefficients() and after each write via the
 * settings_load_subtree("cal") readback in calibration_store_save(). Cells and
 * the validation readback both read from this cache via settings_runtime_get.
 * (There is no global settings_load() in this firmware, so nothing populates
 * this cache implicitly — see calibration_load_coefficients().)
 *
 * @return Pointer to the CELL_MAX_COUNT-element cache array.
 */
static CalCoeff_t *getCalCache(void)
{
    static CalCoeff_t cal_cache[CELL_MAX_COUNT] = {0};
    return cal_cache;
}

/* Parse "cellN" (N = 0..CELL_MAX_COUNT-1) and return the cell index,
 * or -1 if the key doesn't fit that pattern. */
static int32_t cal_parse_cell_key(const char *name)
{
    int32_t result = -1;

    if (0 == strncmp(name, "cell", CAL_KEY_PREFIX_LEN)) {
        char *end = NULL;
        /* strtol() returns long by C standard contract; int64_t safely
         * holds a long on both 32- and 64-bit strtol() implementations. */
        int64_t n = strtol(name + CAL_KEY_PREFIX_LEN, &end, 10);

        if (((name + CAL_KEY_PREFIX_LEN) != end) && ('\0' == *end) &&
            (0 <= n) && (n < (int64_t)CELL_MAX_COUNT)) {
            result = (int32_t)n;
        }
    }

    return result;
}

/* Settings handler set(): called by settings_load_subtree("cal") for
 * each "cal/cellN" key in NVS.  Updates the in-memory cache so cells and
 * validation readbacks see the persisted value. */
static Status_t cal_settings_set(const char *name, size_t len,
                                 settings_read_cb read_cb, void *cb_arg)
{
    Status_t result = 0;
    int32_t cell = cal_parse_cell_key(name);

    (void)len;

    if (0 > cell) {
        result = -ENOENT;
    } else {
        CalCoeff_t value = 0.0f;
        ssize_t got = read_cb(cb_arg, &value, sizeof(value));

        if ((ssize_t)sizeof(value) != got) {
            result = -EIO;
        } else {
            getCalCache()[cell] = value;
        }
    }

    return result;
}

/* Settings handler get(): used by settings_runtime_get("cal/cellN", ...).
 * Returns the cached value's length on success so the caller can size-check.
 *
 * val_len_max stays a bare `int` per Zephyr's settings_handler_static.h_get
 * contract (settings/settings.h): on the arm-zephyr-eabi toolchain int32_t is
 * `long int`, so an int32_t parameter makes this an incompatible function
 * pointer for the handler table. (Status_t is a typedef of int, so the return
 * type carries no such constraint.) */
static Status_t cal_settings_get(const char *name, char *val, int val_len_max)
{
    Status_t result = 0;
    int32_t cell = cal_parse_cell_key(name);

    if (0 > cell) {
        result = -ENOENT;
    } else if ((size_t)val_len_max < sizeof(CalCoeff_t)) {
        result = -EINVAL;
    } else {
        (void)memcpy(val, &getCalCache()[cell], sizeof(CalCoeff_t));
        result = (Status_t)sizeof(CalCoeff_t);
    }

    return result;
}

SETTINGS_STATIC_HANDLER_DEFINE(cal_handler, "cal",
                               cal_settings_get,
                               cal_settings_set,
                               NULL, NULL);

Status_t calibration_store_save(uint8_t cell_num, CalCoeff_t coeff)
{
    char key[CAL_KEY_BUF_LEN] = {0};
    Status_t result = 0;

    (void)snprintk(key, sizeof(key), CAL_SETTINGS_KEY "%u", cell_num);

    result = ext_flash_settings_save_one(key, &coeff, sizeof(coeff));

    if (0 == result) {
        /* Force the cache to reload from NVS so the validation readback in
         * cal_validate_and_save() reflects what actually got persisted to
         * flash, not what we just tried to write.  This catches a class of
         * failures where NVS accepts the write but the backing flash is
         * full/corrupt. */
        result = ext_flash_settings_load_subtree(CAL_SETTINGS_SUBTREE);
    }

    return result;
}

Status_t calibration_store_load(uint8_t cell_num, CalCoeff_t *coeff)
{
    char key[CAL_KEY_BUF_LEN] = {0};
    Status_t result = 0;

    (void)snprintk(key, sizeof(key), CAL_SETTINGS_KEY "%u", cell_num);

    Status_t len = settings_runtime_get(key, coeff, sizeof(*coeff));

    if (len != (Status_t)sizeof(*coeff)) {
        result = -ENOENT;
    }

    return result;
}

/* ---- Boot-time coefficient load (load-once) ----
 *
 * The "cal" subtree is not covered by any boot-time settings_load(): main()
 * only loads the "rt" subtree (runtime_settings_load) and the Zephyr settings
 * subsystem never auto-replays stored values, so without an explicit load the
 * cal_handler set() callback never fires on a fresh boot and getCalCache()
 * stays zero. Each cell's *_load_cal() then reads a default coefficient — the
 * saved calibration sits in flash but is never restored, which presents as
 * "calibration doesn't persist across reboots".
 *
 * The cell threads auto-start (K_THREAD_DEFINE) and run their *_load_cal()
 * during main()'s boot_indicator(), before calibration_init() — so the load
 * must be driven by the consumer, not by init ordering. The mutex + flag makes
 * the first caller perform the NVS read while any concurrent caller blocks
 * until it finishes, then skips; no cell ever reads a half-populated cache and
 * the flash cost is paid once.
 */
/* Framework-mandated file-scope object: K_MUTEX_DEFINE places the control
 * block in an iterable section for compile-time static initialisation, the
 * same pattern SonarQube M23_388 accepts for K_THREAD_DEFINE (see
 * src/i2c_bus_lock.c and docs/SONARQUBE_ACCEPTED_ISSUES.md). */
static K_MUTEX_DEFINE(cal_load_mutex);

/**
 * @brief Return a pointer to the module-static "coefficients loaded" flag.
 *
 * @return Pointer to the bool guarding the one-time NVS coefficient load.
 */
static bool *getCalLoadedFlag(void)
{
    static bool cal_loaded = false;
    return &cal_loaded;
}

void calibration_load_coefficients(void)
{
    (void)k_mutex_lock(&cal_load_mutex, K_FOREVER);

    if (!*getCalLoadedFlag()) {
        /* Idempotent: safe even if the subsystem is already mounted by an
         * earlier runtime_settings_load() / flash_log_init(). */
        Status_t rc = settings_subsys_init();

        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        }
        else {
            rc = ext_flash_settings_load_subtree(CAL_SETTINGS_SUBTREE);
            if (0 != rc) {
                /* Leave the flag clear so a later caller retries; the cells
                 * degrade to their default coefficient in the meantime. */
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
            }
            else {
                *getCalLoadedFlag() = true;
            }
        }
    }

    (void)k_mutex_unlock(&cal_load_mutex);
}

#ifdef CONFIG_ZTEST
void calibration_store_seed_cached(uint8_t cell_num, CalCoeff_t coeff)
{
    if (cell_num < CELL_MAX_COUNT) {
        getCalCache()[cell_num] = coeff;
    }
}
#endif
