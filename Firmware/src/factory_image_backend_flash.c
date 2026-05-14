/**
 * @file factory_image_backend_flash.c
 * @brief Flash-partition backend for factory_image.c (production).
 *
 * Routes every byte through Zephyr's flash_area_* API against the
 * @c factory_partition declared in the board DTS. The captured flag is
 * persisted via the Zephyr settings subsystem on key
 * ``factory/captured`` — same persistence pattern as error_histogram.
 *
 * The flag must NOT be inferred from the partition's content: a partial
 * capture leaves bytes in the partition but no flag set, and the next
 * boot must treat that as "needs capture" rather than "already done".
 *
 * Selected by CONFIG_FACTORY_IMAGE_BACKEND_FLASH (default on
 * divecan_jr); the filesystem-backed alternative lives in
 * factory_image_backend_fs.c (Phase 7).
 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include "factory_image_backend.h"
#include "errors.h"

LOG_MODULE_REGISTER(factory_image_flash, LOG_LEVEL_INF);

/* ---- Settings persistence ---- */

#define FACTORY_FLAG_SUBTREE "factory"
#define FACTORY_FLAG_KEY     FACTORY_FLAG_SUBTREE "/captured"
#define FACTORY_FLAG_LEAF    "captured"

/* ---- Backend state behind a static accessor (M23_388) ---- */

struct backend_state {
    bool initialised;
    bool captured;
};

static struct backend_state *get_state(void)
{
    static struct backend_state state = {0};
    return &state;
}

/* ---- Settings handler: replay flag on init ---- */

static int factory_flag_settings_set(const char *name, size_t len,
                                     settings_read_cb read_cb, void *cb_arg)
{
    int result = -ENOENT;
    ARG_UNUSED(len);

    if (0 == strcmp(name, FACTORY_FLAG_LEAF)) {
        uint8_t value = 0U;
        ssize_t got = read_cb(cb_arg, &value, sizeof(value));

        if (got == (ssize_t)sizeof(value)) {
            get_state()->captured = (0U != value);
            result = 0;
        } else {
            result = -EIO;
        }
    }
    return result;
}

SETTINGS_STATIC_HANDLER_DEFINE(factory_flag_handler, FACTORY_FLAG_SUBTREE,
                               NULL, factory_flag_settings_set, NULL, NULL);

/* ---- Backend ops ---- */

static int flash_backend_init(void)
{
    int result = 0;
    if (!get_state()->initialised) {
        int rc = settings_load_subtree(FACTORY_FLAG_SUBTREE);
        if (0 != rc) {
            LOG_WRN("Settings load (%s) failed: %d", FACTORY_FLAG_SUBTREE, rc);
            /* Treat as "not captured" and continue — the next capture
             * attempt will retry the settings write. */
            result = 0;
        }
        get_state()->initialised = true;
    }
    return result;
}

static int flash_backend_erase(void)
{
    const struct flash_area *fa = NULL;
    int rc = flash_area_open(PARTITION_ID(factory_partition), &fa);
    int result = -EIO;

    if (0 == rc) {
        rc = flash_area_erase(fa, 0, fa->fa_size);
        if (0 == rc) {
            result = 0;
        } else {
            LOG_ERR("flash_area_erase failed: %d", rc);
            result = rc;
        }
        flash_area_close(fa);
    } else {
        LOG_ERR("flash_area_open (factory_partition) failed: %d", rc);
        result = rc;
    }
    return result;
}

static int flash_backend_write(uint32_t offset, const void *buf, size_t len)
{
    const struct flash_area *fa = NULL;
    int rc = flash_area_open(PARTITION_ID(factory_partition), &fa);
    int result = -EIO;

    if (0 == rc) {
        rc = flash_area_write(fa, offset, buf, len);
        if (0 == rc) {
            result = 0;
        } else {
            LOG_ERR("flash_area_write @0x%x len=%zu failed: %d",
                    (unsigned)offset, len, rc);
            result = rc;
        }
        flash_area_close(fa);
    } else {
        result = rc;
    }
    return result;
}

static int flash_backend_read(uint32_t offset, void *buf, size_t len)
{
    const struct flash_area *fa = NULL;
    int rc = flash_area_open(PARTITION_ID(factory_partition), &fa);
    int result = -EIO;

    if (0 == rc) {
        rc = flash_area_read(fa, offset, buf, len);
        if (0 == rc) {
            result = 0;
        } else {
            LOG_ERR("flash_area_read @0x%x len=%zu failed: %d",
                    (unsigned)offset, len, rc);
            result = rc;
        }
        flash_area_close(fa);
    } else {
        result = rc;
    }
    return result;
}

static int flash_backend_flush(void)
{
    /* flash_area_write is synchronous on SPI NOR — nothing to flush. */
    return 0;
}

static int flash_backend_size(uint32_t *out_size)
{
    const struct flash_area *fa = NULL;
    int rc = flash_area_open(PARTITION_ID(factory_partition), &fa);
    int result = -EIO;

    if (0 == rc) {
        if (NULL != out_size) {
            *out_size = (uint32_t)fa->fa_size;
        }
        flash_area_close(fa);
        result = 0;
    } else {
        result = rc;
    }
    return result;
}

static bool flash_backend_is_captured(void)
{
    return get_state()->captured;
}

static int flash_backend_mark_captured(bool captured)
{
    uint8_t value = captured ? 1U : 0U;
    int rc = settings_save_one(FACTORY_FLAG_KEY, &value, sizeof(value));
    int result = -EIO;

    if (0 == rc) {
        get_state()->captured = captured;
        result = 0;
    } else {
        LOG_ERR("settings_save_one (%s) failed: %d", FACTORY_FLAG_KEY, rc);
        result = rc;
    }
    return result;
}

static const struct factory_image_backend flash_backend = {
    .init           = flash_backend_init,
    .erase          = flash_backend_erase,
    .write          = flash_backend_write,
    .read           = flash_backend_read,
    .flush          = flash_backend_flush,
    .size           = flash_backend_size,
    .is_captured    = flash_backend_is_captured,
    .mark_captured  = flash_backend_mark_captured,
};

const struct factory_image_backend *factory_image_get_flash_backend(void)
{
    return &flash_backend;
}
