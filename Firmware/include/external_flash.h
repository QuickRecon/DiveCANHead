/**
 * @file external_flash.h
 * @brief Process-wide gate for blocking external SPI-NOR operations.
 *
 * The W25Q512 hosts MCUBoot slot1/scratch, the factory backup, flash-log FCBs,
 * and Zephyr's NVS/settings partition. Any operation that touches those areas
 * can stall on the same SPI bus/device. Life-critical threads must not call this
 * API directly; they should publish to RAM queues/channels and let service
 * threads perform flash work.
 */
#ifndef EXTERNAL_FLASH_H
#define EXTERNAL_FLASH_H

#include <stddef.h>
#include <sys/types.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Acquire exclusive access to the external flash stack.
 *
 * Use only from startup, service, workqueue, flash-log writer, or
 * programming-session paths. Do not call from PPO2/control/solenoid/cell hot
 * paths.
 */
Status_t external_flash_acquire(k_timeout_t timeout);

/** @brief Release a lock acquired by external_flash_acquire(). */
void external_flash_release(void);

/** @brief Serialised settings_save_one() with bounded transient retry. */
Status_t ext_flash_settings_save_one(const char *name, const void *value,
                                          size_t len);

/** @brief Serialised settings_load_one(). */
ssize_t ext_flash_settings_load_one(const char *name, void *value,
                                         size_t len);

/** @brief Serialised settings_load_subtree(). */
Status_t ext_flash_settings_load_subtree(const char *subtree);

/** @brief Serialised flash_area_read(). */
Status_t external_flash_area_read(const struct flash_area *fa, off_t off,
                                  void *dst, size_t len);

/** @brief Serialised flash_area_write(). */
Status_t external_flash_area_write(const struct flash_area *fa, off_t off,
                                   const void *src, size_t len);

/** @brief Serialised flash_area_erase(). */
Status_t external_flash_area_erase(const struct flash_area *fa, off_t off,
                                   size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EXTERNAL_FLASH_H */
