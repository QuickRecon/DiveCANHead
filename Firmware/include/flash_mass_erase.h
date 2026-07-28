#ifndef FLASH_MASS_ERASE_H
#define FLASH_MASS_ERASE_H

#include "common.h"

/**
 * @file flash_mass_erase.h
 * @brief Whole-chip erase of the external SPI-NOR.
 *
 * Wipes the entire external W25Q512 (slot1/OTA scratch, image-scratch, factory
 * backup, flash-log telemetry+text, and the NVS/settings partition). The
 * internal STM32 flash — MCUboot and the running slot0 image — is NOT touched,
 * so the caller keeps executing through the erase.
 */

/**
 * @brief Chip-erase the entire external SPI-NOR.
 *
 * Uses the device-wide erase (single chip-erase opcode, ~100-400 s) and holds
 * heartbeat_set_long_op() across it so the IWDG stays fed for the duration.
 * After this returns, the external NOR is blank: the flash-log FCB mounts clean
 * and NVS reverts to defaults on the next access.
 *
 * @return 0 on success, negative errno otherwise.
 */
Status_t flash_mass_erase_external(void);

#endif /* FLASH_MASS_ERASE_H */
