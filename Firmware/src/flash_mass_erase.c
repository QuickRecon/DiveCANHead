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

#if DT_NODE_EXISTS(EXT_FLASH_NODE)

/* Erase in chunks so the IWDG is fed between them. Each 256 KiB chunk is
 * 4 x 64 KiB block erases; at the W25Q512JV's ~2 s worst-case per 64 KiB block
 * that is <= ~8 s, comfortably inside the 32 s IWDG window. We feed the dog
 * DIRECTLY here rather than via heartbeat_set_long_op(), because this runs in
 * the divecan_rx thread (the UDS factory-erase path), which preempts the
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

	int first_err = 0;
	unsigned int fail_count = 0U;

	/* Erase the NVS/"storage" partition FIRST. The factory-reset contract
	 * (settings + cal defaults + the boot counter) lives there, at the TOP of
	 * the chip (0x3ff8000). The full linear sweep below occasionally HANGS
	 * mid-way in the SPI NOR driver's no-timeout WIP poll — HW-observed as an
	 * IWDG reset at a RANDOM deep offset (~21 MB one run, further the next),
	 * always long before reaching NVS — so 0xF278 kept failing to reset the
	 * boot counter. Doing NVS up front guarantees the reset even if the bulk
	 * sweep is later cut short. slot1 (offset 0) and the factory backup
	 * (0x47000) sit at LOW offsets and are always reached by the sweep before
	 * any such deep hang, so the rest of the contract still holds. */
#if DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
	{
		off_t  nvs_off = (off_t)DT_REG_ADDR(DT_NODELABEL(storage_partition));
		size_t nvs_sz  = (size_t)DT_REG_SIZE(DT_NODELABEL(storage_partition));

		watchdog_kick();
		int nrc = flash_erase(flash, nvs_off, nvs_sz);
		if (nrc != 0) {
			LOG_ERR("NVS/storage erase @0x%08x failed: %d",
				(unsigned)nvs_off, nrc);
			if (first_err == 0) {
				first_err = nrc;
			}
			fail_count++;
		} else {
			LOG_INF("NVS/storage erased @0x%08x (%u B)",
				(unsigned)nvs_off, (unsigned)nvs_sz);
		}
	}
#endif

	for (uint64_t off = 0; off < total; off += ERASE_CHUNK_BYTES) {
		size_t chunk = (size_t)MIN((uint64_t)ERASE_CHUNK_BYTES, total - off);

		/* Coarse progress log (every 4 MB) so a mid-sweep IWDG reset from a
		 * driver WIP hang leaves the last-reached region visible on RTT/flash
		 * log without spamming 256 lines. */
		if ((off % (16U * ERASE_CHUNK_BYTES)) == 0U) {
			LOG_INF("MASS ERASE @0x%08x", (unsigned)off);
		}

		watchdog_kick();   /* feed before each chunk's (worst-case ~8 s) erase */

		int crc = flash_erase(flash, (off_t)off, chunk);
		/* Erase is idempotent — retry a transient WIP-poll timeout (now bounded
		 * in the spi_nor driver) before giving up on the chunk. */
		for (int attempt = 1; (crc != 0) && (attempt < 3); ++attempt) {
			watchdog_kick();
			crc = flash_erase(flash, (off_t)off, chunk);
		}
		if (crc != 0) {
			/* Do NOT abort the sweep on a single bad chunk. The factory-
			 * reset contract (settings/cal defaults + boot counter) lives
			 * in the NVS/"storage" partition at the TOP of the chip, which
			 * is erased LAST. Returning here on the first failure left NVS
			 * untouched, so 0xF278 silently no-op'd the reset (boot counter
			 * kept climbing — HIL boot_id 35/45 instead of 1). Log the
			 * offending region, keep going so every later partition
			 * (including NVS) still gets wiped, and surface the first rc. */
			LOG_ERR("erase @0x%08x (%u B) failed: %d — continuing",
				(unsigned)off, (unsigned)chunk, crc);
			if (first_err == 0) {
				first_err = crc;
			}
			fail_count++;
		}
	}

	watchdog_kick();
	if (first_err != 0) {
		LOG_ERR("MASS ERASE INCOMPLETE: %u chunk(s) failed, first rc=%d",
			fail_count, first_err);
		return first_err;
	}
	LOG_WRN("MASS ERASE complete — external NOR is blank");
	return 0;
}

#else  /* !DT_NODE_EXISTS(spi_flash) — e.g. native_sim has no external NOR */

int flash_mass_erase_external(void)
{
	LOG_WRN("flash_mass_erase: no external flash node on this build — no-op");
	return -ENODEV;
}

#endif /* DT_NODE_EXISTS(EXT_FLASH_NODE) */
