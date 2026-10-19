#ifndef FLASH_MASS_ERASE_H
#define FLASH_MASS_ERASE_H

#include "common.h"

/**
 * @file flash_mass_erase.h
 * @brief Whole-range erase of the external flash-compatible device.
 *
 * Wipes the entire external 64 MiB range (slot1/OTA scratch, image-scratch,
 * factory backup, flash-log telemetry+text, and the NVS/settings partition).
 * The internal STM32 flash — MCUboot and the running slot0 image — is NOT
 * touched, so the caller keeps executing through the erase.
 */

/**
 * @brief Erase the entire external storage range.
 *
 * Uses bounded chunks and feeds the IWDG between them. After this returns, the
 * external range contains the backend's flash erase value (`0xff`): the
 * flash-log FCB mounts clean and NVS reverts to defaults on the next access.
 *
 * @return 0 on success, negative errno otherwise.
 */
Status_t flash_mass_erase_external(void);

#endif /* FLASH_MASS_ERASE_H */
