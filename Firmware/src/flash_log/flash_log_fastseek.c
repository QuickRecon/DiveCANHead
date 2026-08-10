/**
 * @file flash_log_fastseek.c
 * @brief Bulk-read FCB active-sector cursor seek.
 *
 * Standalone TU (no flash_log.c dependencies) so the native
 * flash_log_fastseek ztest can prove cursor parity against the stock
 * fcb_init entry walk on the flash simulator — the regression guard for
 * the two boot-time cursor bugs this parser shipped with (terminator
 * chunk-progress loss, and stalling instead of hopping a decoded length
 * that overruns the chunk).
 */

#include "flash_log_fastseek.h"

#include <zephyr/storage/flash_map.h>

#include "common.h"

/* FCB entry length-field encoding (FCB_FLAGS_CRC_DISABLED, f_align=1): the
 * on-flash length bytes are XORed against ~erase_value; bit 7 of the first
 * byte selects the 2-byte form, which carries 7 low bits in byte 0 and the
 * rest in byte 1. Every entry ends with a fixed 1-byte end marker. */
#define FL_ENTRY_LEN2_FLAG   0x80U /* first length byte: a second byte follows */
#define FL_ENTRY_LEN1_MASK   0x7FU /* payload bits carried by the first byte */
#define FL_ENTRY_LEN2_SHIFT  7U    /* second byte's contribution starts at bit 7 */
#define FL_ENTRY_MARKER_SZ   1U    /* fixed end marker (0xAB) */
#define FL_ENTRY_PEEK_BYTES  2U    /* max length-field size == terminator size */

/**
 * @brief Decode one entry length field from a chunk buffer.
 *
 * @param hdr       Points at the entry's first length byte (at least
 *                  FL_ENTRY_PEEK_BYTES readable).
 * @param ev        Flash erase value the field is XOR-encoded against.
 * @param data_len  Out: decoded entry payload length.
 * @return Size of the length field itself (1 or 2 bytes).
 */
static uint32_t fl_entry_decode_len(const uint8_t *hdr, uint8_t ev,
                                    uint16_t *data_len)
{
    uint8_t not_ev = (uint8_t)(~(uint32_t)ev);
    uint8_t b0_xor = hdr[0] ^ not_ev;
    uint32_t len_sz = 1U;

    if (0U != (b0_xor & FL_ENTRY_LEN2_FLAG)) {
        uint8_t b1_xor = hdr[1] ^ not_ev;

        *data_len = (uint16_t)((uint16_t)(b0_xor & FL_ENTRY_LEN1_MASK) |
                               (uint16_t)((uint16_t)b1_xor << FL_ENTRY_LEN2_SHIFT));
        len_sz = FL_ENTRY_PEEK_BYTES;
    } else {
        *data_len = (uint16_t)b0_xor;
    }
    return len_sz;
}

/** Result of scanning one bulk-read chunk for whole FCB entries. */
struct fl_chunk_scan {
    uint32_t consumed; /**< Bytes of whole entries parsed from the chunk. */
    uint32_t hop;      /**< Skip distance for an entry spanning past the chunk. */
    uint32_t entries;  /**< Whole entries counted. */
    bool at_end;       /**< Hit the erased-flash terminator (ev ev). */
};

/**
 * @brief Walk entry headers inside one RAM chunk of the active sector.
 *
 * Mirrors stock fcb_getnext semantics exactly: a decoded length that runs
 * past the chunk is hopped whether or not its end marker would validate (the
 * stock EBADMSG path hops identically) — this is what keeps the cursor
 * bit-exact with fcb_getnext on rings that carry torn/garbage regions.
 */
static void fl_scan_chunk(const uint8_t *buf, uint32_t chunk, uint8_t ev,
                          struct fl_chunk_scan *scan)
{
    uint32_t pos = 0U;
    bool stop = false;

    scan->hop = 0U;
    scan->entries = 0U;
    scan->at_end = false;

    while ((!stop) && ((pos + FL_ENTRY_PEEK_BYTES) <= chunk)) {
        if ((buf[pos] == ev) && (buf[pos + 1U] == ev)) {
            /* Loop guard guarantees pos+1 is in-buffer. */
            scan->at_end = true;
            stop = true;
        } else {
            uint16_t data_len = 0U;
            uint32_t len_sz = fl_entry_decode_len(&buf[pos], ev, &data_len);
            uint32_t entry_total = len_sz + (uint32_t)data_len + FL_ENTRY_MARKER_SZ;

            if (entry_total > (chunk - pos)) {
                scan->hop = entry_total;
                stop = true;
            } else {
                pos += entry_total;
                scan->entries += 1U;
            }
        }
    }
    scan->consumed = pos;
}

void fl_fast_seek_active(struct fcb *fcb_p,
                         volatile struct fl_mount_stats *stats,
                         uint8_t *scratch, uint32_t scratch_len)
{
    const struct flash_sector *sector = fcb_p->f_active.fe_sector;
    uint32_t sector_size = sector->fs_size;
    uint32_t cursor = fcb_p->f_active.fe_elem_off;
    const uint8_t ev = fcb_p->f_erase_value;
    uint32_t entries = 0U;
    uint32_t reads = 0U;
    bool done = false;

    while ((!done) && (cursor < sector_size)) {
        uint32_t remain = sector_size - cursor;
        uint32_t chunk = scratch_len;

        if (remain < scratch_len) {
            chunk = remain;
        }
        Status_t rc = flash_area_read(fcb_p->fap,
                                      sector->fs_off + (off_t)cursor,
                                      scratch, chunk);
        reads += 1U;
        if (0 != rc) {
            done = true;
        } else {
            struct fl_chunk_scan scan = { 0 };

            fl_scan_chunk(scratch, chunk, ev, &scan);
            entries += scan.entries;
            if (scan.at_end) {
                /* Entries parsed before the terminator still count. */
                cursor += scan.consumed;
                done = true;
            } else {
                cursor += scan.consumed + scan.hop;
                if (cursor > sector_size) {
                    cursor = sector_size;
                }
                if ((0U == scan.consumed) && (0U == scan.hop)) {
                    /* <FL_ENTRY_PEEK_BYTES left in sector — nothing decodable. */
                    done = true;
                }
            }
        }
    }
    fcb_p->f_active.fe_elem_off = cursor;
    if (stats != NULL) {
        stats->sector_count = fcb_p->f_sector_cnt;
        stats->active_entries = entries;
        stats->active_bytes = cursor;
        stats->bulk_reads = reads;
    }
}
