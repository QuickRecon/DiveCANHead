/**
 * @file flash_log_index.c
 * @brief Pure helpers over the reader's in-RAM sector index.
 *
 * Kept separate from flash_log_reader.c so the summarizer can be
 * unit-tested without linking the FCB stack. The reader path owns
 * the index storage and the walk that fills it; this TU only knows
 * how to reduce a fully-built index array to a summary.
 */

#include "flash_log_internal.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/**
 * @brief Fold one HAS_BOOT sector row into the running boot summary.
 * @param e Index row known to carry FL_INDEX_FLAG_HAS_BOOT.
 * @param out Summary being accumulated.
 * @param seen_boot In/out flag: whether any valid boot id has been folded yet.
 */
static void fl_summarize_boot(const FlashLogIndexEntry_t *e,
                              FlashLogIndexSummary_t *out,
                              bool *seen_boot)
{
    uint32_t bid = e->first_boot_id;

    ++out->boot_count;
    if (bid != FL_INVALID_BOOT_ID) {
        if (!*seen_boot) {
            out->boot_id_oldest = bid;
            out->boot_id_latest = bid;
            *seen_boot = true;
        } else if (bid < out->boot_id_oldest) {
            out->boot_id_oldest = bid;
        } else if (bid > out->boot_id_latest) {
            out->boot_id_latest = bid;
        } else {
            /* No action required */
        }
    }
}

/**
 * @brief Fold one HAS_DIVE_START sector row into the running dive summary.
 * @param e Index row known to carry FL_INDEX_FLAG_HAS_DIVE_START.
 * @param out Summary being accumulated.
 * @param seen_dive_start In/out flag: whether any valid dive id has been folded.
 */
static void fl_summarize_dive(const FlashLogIndexEntry_t *e,
                              FlashLogIndexSummary_t *out,
                              bool *seen_dive_start)
{
    uint16_t did = e->first_dive_id;

    ++out->dive_count;
    if ((did != FL_INVALID_DIVE_ID) &&
        ((!*seen_dive_start) || (did > out->dive_id_latest))) {
        out->dive_id_latest = did;
        *seen_dive_start = true;
    }
}

void flash_log_index_summarize(const FlashLogIndexEntry_t *index,
                               size_t count,
                               FlashLogIndexSummary_t *out)
{
    if (out != NULL) {
        (void)memset(out, 0, sizeof(*out));

        if ((index != NULL) && (count != 0U)) {
            bool seen_boot = false;
            bool seen_dive_start = false;

            for (size_t i = 0; i < count; ++i) {
                if (0U != (index[i].flags & FL_INDEX_FLAG_HAS_BOOT)) {
                    fl_summarize_boot(&index[i], out, &seen_boot);
                }
                if (0U != (index[i].flags & FL_INDEX_FLAG_HAS_DIVE_START)) {
                    fl_summarize_dive(&index[i], out, &seen_dive_start);
                }
            }
        }
    }
}
