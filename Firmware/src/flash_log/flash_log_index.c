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
                    uint32_t bid = index[i].first_boot_id;

                    ++out->boot_count;
                    if (bid != FL_INVALID_BOOT_ID) {
                        if (!seen_boot) {
                            out->boot_id_oldest = bid;
                            out->boot_id_latest = bid;
                            seen_boot = true;
                        } else {
                            if (bid < out->boot_id_oldest) {
                                out->boot_id_oldest = bid;
                            }
                            if (bid > out->boot_id_latest) {
                                out->boot_id_latest = bid;
                            }
                        }
                    }
                }
                if (0U != (index[i].flags & FL_INDEX_FLAG_HAS_DIVE_START)) {
                    uint16_t did = index[i].first_dive_id;

                    ++out->dive_count;
                    if ((did != FL_INVALID_DIVE_ID) &&
                        (!seen_dive_start || (did > out->dive_id_latest))) {
                        out->dive_id_latest = did;
                        seen_dive_start = true;
                    }
                }
            }
        }
    }
}
