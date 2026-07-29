/**
 * @file external_flash.c
 * @brief Shared external SPI-NOR coordination.
 */

#include "external_flash.h"

#include <errno.h>

#include <zephyr/settings/settings.h>

#define EXT_FLASH_SAVE_RETRY_DELAY_MS 25
#define EXT_FLASH_SAVE_RETRY_ATTEMPTS 40

static K_MUTEX_DEFINE(external_flash_mutex);

Status_t external_flash_acquire(k_timeout_t timeout)
{
    __ASSERT(!k_is_in_isr(), "external flash lock cannot be taken from ISR");
    return k_mutex_lock(&external_flash_mutex, timeout);
}

void external_flash_release(void)
{
    (void)k_mutex_unlock(&external_flash_mutex);
}

Status_t external_flash_settings_save_one(const char *name, const void *value,
                                          size_t len)
{
    Status_t rc = external_flash_acquire(K_FOREVER);

    if (0 == rc) {
        rc = settings_save_one(name, value, len);
        for (uint8_t attempt = 1U;
             ((-EBUSY == rc) || (-EAGAIN == rc)) &&
             (attempt < EXT_FLASH_SAVE_RETRY_ATTEMPTS);
             ++attempt) {
            k_msleep(EXT_FLASH_SAVE_RETRY_DELAY_MS);
            rc = settings_save_one(name, value, len);
        }
        external_flash_release();
    }
    return rc;
}

ssize_t external_flash_settings_load_one(const char *name, void *value,
                                         size_t len)
{
    ssize_t result = -EBUSY;

    if (0 == external_flash_acquire(K_FOREVER)) {
        result = settings_load_one(name, value, len);
        external_flash_release();
    }
    return result;
}

Status_t external_flash_settings_load_subtree(const char *subtree)
{
    Status_t rc = external_flash_acquire(K_FOREVER);

    if (0 == rc) {
        rc = settings_load_subtree(subtree);
        external_flash_release();
    }
    return rc;
}

Status_t external_flash_area_read(const struct flash_area *fa, off_t off,
                                  void *dst, size_t len)
{
    Status_t rc = external_flash_acquire(K_FOREVER);

    if (0 == rc) {
        rc = flash_area_read(fa, off, dst, len);
        external_flash_release();
    }
    return rc;
}

Status_t external_flash_area_write(const struct flash_area *fa, off_t off,
                                   const void *src, size_t len)
{
    Status_t rc = external_flash_acquire(K_FOREVER);

    if (0 == rc) {
        rc = flash_area_write(fa, off, src, len);
        external_flash_release();
    }
    return rc;
}

Status_t external_flash_area_erase(const struct flash_area *fa, off_t off,
                                   size_t len)
{
    Status_t rc = external_flash_acquire(K_FOREVER);

    if (0 == rc) {
        rc = flash_area_erase(fa, off, len);
        external_flash_release();
    }
    return rc;
}
