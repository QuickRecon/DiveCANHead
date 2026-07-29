/**
 * @file helper_normal.c
 * @brief Ordinary (non-forced, non-self) log source used to drive the
 *        backend's threshold pass/drop arms and the render/truncate path.
 */
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_normal_mod, LOG_LEVEL_DBG);

void hn_emit_inf(void)
{
    LOG_INF("normal-inf-line");
}

void hn_emit_newline_terminated(void)
{
    /* A message whose rendered form ends in '\n' exercises the trailing-newline
     * strip in upb_process(). */
    LOG_INF("trailing-newline\n");
}

void hn_emit_long(void)
{
    /* Far longer than UDS_LOG_MAX_PAYLOAD (253) so upb_data_out() must
     * truncate: the copy-clamp and the remaining==0 short-circuit both fire. */
    LOG_INF("%s",
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
            "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC"
            "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"
            "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE"
            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
            "GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG");
}
