/**
 * @file option_bytes.h
 * @brief Boot-time option-byte assertion and brown-out self-heal.
 */
#ifndef OPTION_BYTES_H
#define OPTION_BYTES_H

#include "common.h"

/**
 * @brief Check the asserted option bytes and self-heal BOR_LEV if it drifted.
 *
 * Registered as a SYS_INIT hook; exposed so the native test suite can drive it
 * against a mocked HAL and prove the write is confined to BOR_LEV. Never
 * returns if a programming attempt succeeds — HAL_FLASH_OB_Launch() resets the
 * SoC to load the new option bytes.
 *
 * @return 0 always (SYS_INIT contract; a wrong option byte is reported through
 *         the log, never by failing init, because refusing to boot over a
 *         brown-out level would be worse than running at the wrong one).
 */
Status_t option_bytes_check_and_apply(void);

#endif /* OPTION_BYTES_H */
