/**
 * @file flash_mass_erase.c
 * @brief Whole-chip erase of the external SPI-NOR (see flash_mass_erase.h).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include "flash_mass_erase.h"
#include "watchdog_feeder.h"

LOG_MODULE_REGISTER(flash_mass_erase, LOG_LEVEL_INF);

#define EXT_FLASH_NODE DT_NODELABEL(spi_flash)

/* Erase in chunks so the IWDG is fed between them. Each 256 KiB chunk is
 * 4 x 64 KiB block erases; at the W25Q512JV's ~2 s worst-case per 64 KiB block
 * that is <= ~8 s, comfortably inside the 32 s IWDG window. We feed the dog
 * DIRECTLY here rather than via heartbeat_set_long_op(), because this runs in a
 * higher-priority thread (main, on the one-shot boot path) that preempts the
 * lowest-priority feeder thread — long_op alone would not save us. */
#define ERASE_CHUNK_BYTES   (256U * 1024U)

int flash_mass_erase_external(void)
{
	const struct device *flash = DEVICE_DT_GET(EXT_FLASH_NODE);

	if (!device_is_ready(flash)) {
		LOG_ERR("external flash not ready");
		return -ENODEV;
	}

	uint64_t total = 0;
	int rc = flash_get_size(flash, &total);

	if ((rc != 0) || (total == 0U)) {
		LOG_ERR("flash_get_size failed: %d", rc);
		return (rc != 0) ? rc : -EIO;
	}

	LOG_WRN("MASS ERASE: wiping %u KiB of external NOR in %u KiB chunks "
		"(slot1/OTA, factory, flash-log, NVS) — takes minutes, IWDG-fed",
		(unsigned)(total / 1024U), (unsigned)(ERASE_CHUNK_BYTES / 1024U));

	for (uint64_t off = 0; off < total; off += ERASE_CHUNK_BYTES) {
		size_t chunk = (size_t)MIN((uint64_t)ERASE_CHUNK_BYTES, total - off);

		watchdog_kick();   /* feed before each chunk's (worst-case ~8 s) erase */

		rc = flash_erase(flash, (off_t)off, chunk);
		if (rc != 0) {
			LOG_ERR("erase @0x%08x (%u B) failed: %d",
				(unsigned)off, (unsigned)chunk, rc);
			return rc;
		}
	}

	watchdog_kick();
	LOG_WRN("MASS ERASE complete — external NOR is blank");
	return 0;
}
