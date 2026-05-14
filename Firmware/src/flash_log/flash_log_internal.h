/**
 * @file flash_log_internal.h
 * @brief Accessors shared between flash_log.c and the reader / UDS path.
 *
 * Not part of the public producer API in include/flash_log.h — these
 * are intentionally bound to the implementation so producers can't
 * reach into the FCB instance state.
 */
#ifndef FLASH_LOG_INTERNAL_H
#define FLASH_LOG_INTERNAL_H

#include <stdint.h>
#include <zephyr/fs/fcb.h>

#include "flash_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Hand out the FCB instance for the given destination, or NULL. */
struct fcb *flash_log_internal_get_fcb(FlashLogDest_t dest);

/** @brief Number of logical sectors per FCB. */
uint8_t flash_log_internal_sector_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_LOG_INTERNAL_H */
