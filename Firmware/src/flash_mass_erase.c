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
#include "external_flash.h"
#include "watchdog_feeder.h"
#include "common.h"

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

/* Progress log cadence: one line every 16 chunks (~4 MB). */
#define PROGRESS_LOG_CHUNK_INTERVAL 16U

/* Retry a transient WIP-poll timeout this many times before giving up on a
 * chunk (erase is idempotent). */
#define ERASE_RETRY_ATTEMPTS 3U

#if DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
/**
 * @brief Erase the NVS/"storage" partition (top of chip) before the bulk sweep.
 *
 * The factory-reset contract (settings + cal defaults + the boot counter) lives
 * there. The full linear sweep occasionally HANGS mid-way in the SPI NOR
 * driver's no-timeout WIP poll — HW-observed as an IWDG reset at a RANDOM deep
 * offset, always long before reaching NVS. Doing NVS up front guarantees the
 * reset even if the bulk sweep is later cut short.
 *
 * @param flash Ready external-flash device.
 * @return 0 on success, negative errno on erase failure.
 */
static Status_t erase_nvs_partition(const struct device *flash)
{
    off_t  nvs_off = (off_t)DT_REG_ADDR(DT_NODELABEL(storage_partition));
    size_t nvs_sz  = (size_t)DT_REG_SIZE(DT_NODELABEL(storage_partition));

    watchdog_kick();
    Status_t nrc = flash_erase(flash, nvs_off, nvs_sz);
    if (nrc != 0) {
        LOG_ERR("NVS/storage erase @0x%08x failed: %d",
            (unsigned)nvs_off, nrc);
    } else {
        LOG_INF("NVS/storage erased @0x%08x (%u B)",
            (unsigned)nvs_off, (unsigned)nvs_sz);
    }
    return nrc;
}
#endif

Status_t flash_mass_erase_external(void)
{
    Status_t result = 0;
    const struct device *flash = DEVICE_DT_GET(EXT_FLASH_NODE);

    if (!device_is_ready(flash)) {
        LOG_ERR("external flash not ready");
        result = -ENODEV;
    } else {
        result = external_flash_acquire(K_FOREVER);
        if (0 == result) {
            uint64_t total = 0;
            Status_t rc = flash_get_size(flash, &total);

            if ((rc != 0) || (total == 0U)) {
                LOG_ERR("flash_get_size failed: %d", rc);
                if (rc != 0) {
                    result = rc;
                } else {
                    result = -EIO;
                }
            } else {
                LOG_WRN("MASS ERASE: wiping %u KiB of external NOR in %u KiB chunks "
                    "(slot1/OTA, factory, flash-log, NVS) — takes minutes, IWDG-fed",
                    (unsigned)(total / 1024U), (unsigned)(ERASE_CHUNK_BYTES / 1024U));

                Status_t first_err = 0;
                uint32_t fail_count = 0U;

                /* Erase the NVS/"storage" partition FIRST — see erase_nvs_partition().
                 * slot1 (offset 0) and the factory backup (0x47000) sit at LOW
                 * offsets and are always reached by the sweep before any deep hang,
                 * so the rest of the contract still holds. */
#if DT_NODE_EXISTS(DT_NODELABEL(storage_partition))
                Status_t nrc = erase_nvs_partition(flash);
                if (nrc != 0) {
                    if (first_err == 0) {
                        first_err = nrc;
                    }
                    ++fail_count;
                }
#endif

                for (uint64_t off = 0; off < total; off += ERASE_CHUNK_BYTES) {
                    size_t chunk = (size_t)MIN((uint64_t)ERASE_CHUNK_BYTES,
                                               total - off);

                    /* Coarse progress log so a mid-sweep IWDG reset from a driver
                     * WIP hang leaves the last-reached region visible without
                     * spamming 256 lines. */
                    if ((off % (PROGRESS_LOG_CHUNK_INTERVAL * ERASE_CHUNK_BYTES)) == 0U) {
                        LOG_INF("MASS ERASE @0x%08x", (unsigned)off);
                    }

                    watchdog_kick();   /* feed before each chunk's (worst-case ~8 s) erase */

                    Status_t crc = flash_erase(flash, (off_t)off, chunk);
                    /* Erase is idempotent — retry a transient WIP-poll timeout
                     * (now bounded in the spi_nor driver) before giving up. */
                    uint8_t attempt = 1U;
                    while ((crc != 0) && (attempt < ERASE_RETRY_ATTEMPTS)) {
                        watchdog_kick();
                        crc = flash_erase(flash, (off_t)off, chunk);
                        ++attempt;
                    }
                    if (crc != 0) {
                        /* Do NOT abort the sweep on a single bad chunk. NVS at the
                         * TOP of the chip is erased FIRST (above), so the factory-
                         * reset contract holds even if a later chunk fails. Log the
                         * offending region, keep going, and surface the first rc. */
                        LOG_ERR("erase @0x%08x (%u B) failed: %d — continuing",
                            (unsigned)off, (unsigned)chunk, crc);
                        if (first_err == 0) {
                            first_err = crc;
                        }
                        ++fail_count;
                    }
                }

                watchdog_kick();
                if (first_err != 0) {
                    LOG_ERR("MASS ERASE INCOMPLETE: %u chunk(s) failed, first rc=%d",
                        fail_count, first_err);
                    result = first_err;
                } else {
                    LOG_WRN("MASS ERASE complete — external NOR is blank");
                }
            }
            external_flash_release();
        }
    }
    return result;
}

#else  /* !DT_NODE_EXISTS(spi_flash) — e.g. native_sim has no external NOR */

Status_t flash_mass_erase_external(void)
{
    LOG_WRN("flash_mass_erase: no external flash node on this build — no-op");
    return -ENODEV;
}

#endif /* DT_NODE_EXISTS(EXT_FLASH_NODE) */
