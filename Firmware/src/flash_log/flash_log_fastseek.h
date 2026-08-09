/**
 * @file flash_log_fastseek.h
 * @brief Bulk-read FCB active-sector cursor seek (see flash_log_fastseek.c).
 */
#ifndef FLASH_LOG_FASTSEEK_H
#define FLASH_LOG_FASTSEEK_H

#include <stdint.h>
#include <zephyr/fs/fcb.h>

/** Boot-time FCB mount diagnostics — readable via debugger after boot. */
struct fl_mount_stats {
    uint32_t sector_count;
    uint32_t active_entries;
    uint32_t active_bytes;
    uint32_t bulk_reads;
};

/**
 * @brief Seek the FCB active-sector write cursor using bulk flash reads.
 *
 * fcb_init with FCB_FLAGS_INIT_SKIP_WALK leaves f_active.fe_elem_off at the
 * sector header. This reads the active sector in scratch_len chunks and walks
 * entry headers in RAM to find the write cursor, replacing the stock
 * per-entry SPI reads whose per-transaction framework overhead dominated boot
 * time (~1930 tiny reads → ~35 bulk reads on a full 256 KiB sector).
 *
 * Entry format (FCB_FLAGS_CRC_DISABLED, f_align=1, erase_value=0xFF):
 *   len_field (1 byte if <128, else 2 bytes) + data (len bytes) + 0xAB
 *   End of entries: 0xFF 0xFF (erased flash).
 *
 * @param fcb_p       Initialised FCB (post fcb_init with SKIP_WALK).
 * @param stats       Optional mount diagnostics out; NULL to skip.
 * @param scratch     Chunk buffer (idle at init time — the caller's write
 *                    batching buffer is repurposed on the firmware side).
 * @param scratch_len Usable scratch bytes (must be >= 2).
 */
void fl_fast_seek_active(struct fcb *fcb_p,
                         volatile struct fl_mount_stats *stats,
                         uint8_t *scratch, uint32_t scratch_len);

#endif /* FLASH_LOG_FASTSEEK_H */
