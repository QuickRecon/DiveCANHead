/**
 * @file main.c
 * @brief ISO-TP framing layer unit tests
 *
 * Tests the ISO-TP RX state machine (ISOTP_ProcessRxFrame, ISOTP_Poll) and the
 * TX queue (ISOTP_Send, ISOTP_TxQueue_Poll, ISOTP_TxQueue_ProcessFC) in
 * isotp.c / isotp_tx_queue.c. Uses divecan_tx_stub.c in place of the real CAN
 * driver so outgoing frames (flow-control responses, SF, FF, CF) can be
 * inspected by byte index.
 *
 * DiveCAN-specific padding: the real protocol inserts a 0x00 byte after the
 * PCI byte in every frame, so SF/FF data starts at byte[2] not byte[1].
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "isotp.h"
#include "isotp_tx_queue.h"
#include "divecan_tx_stub.h"

#define SRC DIVECAN_SOLO
#define TGT DIVECAN_CONTROLLER
#define MSG_ID MENU_ID

static ISOTPContext_t ctx;

/** @brief Build a DiveCANMessage_t with the DiveCAN ID encoding (target<<8 | source). */
static DiveCANMessage_t make_msg(uint8_t src, uint8_t tgt, const uint8_t *data,
                 uint8_t len)
{
    DiveCANMessage_t m = {
        .id = MSG_ID | ((uint32_t)tgt << 8) | (uint32_t)src,
        .length = len,
    };
    if (data != NULL) {
        (void)memcpy(m.data, data, len);
    }
    return m;
}

/**
 * @brief Suite-level setup: initialise the TX queue and the ISO-TP context once per suite run.
 *
 * Called by the ztest framework before the first test in each suite; also mirrors
 * isotp_before so the context is clean even without a prior test.
 */
static void *isotp_setup(void)
{
    test_reset_frames();
    ISOTP_TxQueue_Init();
    ISOTP_Init(&ctx, SRC, TGT, MSG_ID);
    return NULL;
}

/**
 * @brief Per-test setup: reinitialise the frame buffer, TX queue, and context.
 *
 * Runs before every test in both suites so each test starts with a clean state:
 * no captured frames, no in-progress TX, and context in ISOTP_IDLE.
 */
static void isotp_before(void *fixture)
{
    ARG_UNUSED(fixture);
    test_reset_frames();
    ISOTP_TxQueue_Init();
    ISOTP_Init(&ctx, SRC, TGT, MSG_ID);
}

/** @brief Suite: ISO-TP receive path — SF, FF+CF reassembly, error and address filtering. */
ZTEST_SUITE(isotp_rx, NULL, isotp_setup, isotp_before, NULL, NULL);
/** @brief Suite: ISO-TP transmit path — SF/FF/CF generation, FC handling, queue serialization. */
ZTEST_SUITE(isotp_tx, NULL, isotp_setup, isotp_before, NULL, NULL);

/** @brief A valid 3-byte SF is accepted, rxComplete is set, and bytes are copied to rxBuffer. */
ZTEST(isotp_rx, test_sf_basic)
{
    uint8_t data[] = {0x03, 0xAA, 0xBB, 0xCC};
    DiveCANMessage_t msg = make_msg(TGT, SRC, data, 4);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &msg);

    zassert_true(consumed);
    zassert_true(ctx.rxComplete);
    zassert_equal(ctx.rxDataLength, 3);
    zassert_equal(ctx.rxBuffer[0], 0xAA);
    zassert_equal(ctx.rxBuffer[1], 0xBB);
    zassert_equal(ctx.rxBuffer[2], 0xCC);
}

/** @brief Maximum SF payload (7 bytes — the ISO-TP limit for a CAN SF) is fully received. */
ZTEST(isotp_rx, test_sf_max_length)
{
    uint8_t data[] = {0x07, 1, 2, 3, 4, 5, 6, 7};
    DiveCANMessage_t msg = make_msg(TGT, SRC, data, 8);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &msg);

    zassert_true(consumed);
    zassert_true(ctx.rxComplete);
    zassert_equal(ctx.rxDataLength, 7);
    for (int i = 0; i < 7; i++) {
        zassert_equal(ctx.rxBuffer[i], (uint8_t)(i + 1));
    }
}

/** @brief An SF with a zero length field is rejected (not consumed, rxComplete stays false). */
ZTEST(isotp_rx, test_sf_zero_length_rejected)
{
    uint8_t data[] = {0x00, 0xAA};
    DiveCANMessage_t msg = make_msg(TGT, SRC, data, 2);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &msg);

    zassert_false(consumed);
    zassert_false(ctx.rxComplete);
}

/** @brief A frame addressed to a different target is silently ignored (not consumed). */
ZTEST(isotp_rx, test_sf_wrong_target_ignored)
{
    uint8_t data[] = {0x03, 0xAA, 0xBB, 0xCC};
    DiveCANMessage_t msg = make_msg(TGT, DIVECAN_MONITOR, data, 4);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &msg);

    zassert_false(consumed);
    zassert_false(ctx.rxComplete);
}

/** @brief FF followed by one CF reassembles 10 bytes correctly; a FC is sent after the FF. */
ZTEST(isotp_rx, test_multiframe_reassembly)
{
    /* 10 bytes total: FF carries 6, CF carries remaining 4 */
    uint8_t ff_data[] = {0x10, 10, 1, 2, 3, 4, 5, 6};
    DiveCANMessage_t ff = make_msg(TGT, SRC, ff_data, 8);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &ff);
    zassert_true(consumed);
    zassert_false(ctx.rxComplete);
    zassert_equal(ctx.state, ISOTP_RECEIVING);

    /* FC should have been sent */
    zassert_equal(test_get_frame_count(), 1);
    const DiveCANMessage_t *fc = test_get_last_frame();
    zassert_equal(fc->data[0] & 0xF0, 0x30); /* FC PCI */

    /* CF with seq=1 */
    uint8_t cf_data[] = {0x21, 7, 8, 9, 10, 0, 0, 0};
    DiveCANMessage_t cf = make_msg(TGT, SRC, cf_data, 8);

    consumed = ISOTP_ProcessRxFrame(&ctx, &cf);
    zassert_true(consumed);
    zassert_true(ctx.rxComplete);
    zassert_equal(ctx.rxDataLength, 10);

    for (int i = 0; i < 10; i++) {
        zassert_equal(ctx.rxBuffer[i], (uint8_t)(i + 1),
                  "byte %d: expected %d got %d", i, i + 1,
                  ctx.rxBuffer[i]);
    }
}

/** @brief An FF declaring more than 256 bytes is rejected with a Flow Control OVFLW frame. */
ZTEST(isotp_rx, test_ff_overlength_rejected)
{
    /* Length > 256 → overflow */
    uint8_t ff_data[] = {0x12, 0x00, 0, 0, 0, 0, 0, 0}; /* 0x200 = 512 */
    DiveCANMessage_t ff = make_msg(TGT, SRC, ff_data, 8);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &ff);
    zassert_true(consumed); /* FF always consumed even on reject */
    zassert_false(ctx.rxComplete);

    /* FC OVFLW should have been sent */
    const DiveCANMessage_t *fc = test_get_last_frame();
    zassert_not_null(fc);
    zassert_equal(fc->data[0], 0x32); /* OVFLW status */
}

/** @brief A CF with an out-of-order sequence number aborts reception and resets to ISOTP_IDLE. */
ZTEST(isotp_rx, test_cf_wrong_sequence)
{
    /* Start multi-frame */
    uint8_t ff_data[] = {0x10, 14, 1, 2, 3, 4, 5, 6};
    DiveCANMessage_t ff = make_msg(TGT, SRC, ff_data, 8);
    (void)ISOTP_ProcessRxFrame(&ctx, &ff);

    /* CF with seq=2 instead of expected seq=1 */
    uint8_t cf_data[] = {0x22, 7, 8, 9, 10, 11, 12, 13};
    DiveCANMessage_t cf = make_msg(TGT, SRC, cf_data, 8);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &cf);
    zassert_true(consumed);
    zassert_false(ctx.rxComplete);
    zassert_equal(ctx.state, ISOTP_IDLE); /* Reset on error */
}

/** @brief A CF arriving while the context is in ISOTP_IDLE is rejected without side effects. */
ZTEST(isotp_rx, test_cf_in_idle_rejected)
{
    uint8_t cf_data[] = {0x21, 1, 2, 3, 4, 5, 6, 7};
    DiveCANMessage_t cf = make_msg(TGT, SRC, cf_data, 8);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &cf);
    zassert_false(consumed);
}

/**
 * @brief Shearwater quirk: FC from source=0xFF (broadcast) is handled by the TX queue, not RX context.
 *
 * The Shearwater sends FC frames with source=0xFF. This test verifies the RX
 * context does not crash on such a frame; the TX queue's FC processor handles it.
 */
ZTEST(isotp_rx, test_shearwater_fc_quirk)
{
    /* Shearwater sends FC with source=0xFF (broadcast) */
    uint8_t fc_data[] = {0x30, 0, 0, 0, 0, 0, 0, 0}; /* CTS */
    DiveCANMessage_t fc = {
        .id = MSG_ID | ((uint32_t)SRC << 8) | 0xFF,
        .length = 3,
    };
    (void)memcpy(fc.data, fc_data, 3);

    /* This should be accepted by the TX queue FC processor,
     * but only if there's an active TX waiting for FC.
     * Just verify it doesn't crash and the context accepts the address. */
    bool consumed = ISOTP_ProcessRxFrame(&ctx, &fc);
    /* FC is handled by TX queue, not RX context — returns false here */
    zassert_false(consumed);
}

/** @brief N_Cr timeout: context resets to ISOTP_IDLE when no CF arrives within 1000 ms. */
ZTEST(isotp_rx, test_ncr_timeout)
{
    /* Start multi-frame reception */
    uint8_t ff_data[] = {0x10, 14, 1, 2, 3, 4, 5, 6};
    DiveCANMessage_t ff = make_msg(TGT, SRC, ff_data, 8);
    (void)ISOTP_ProcessRxFrame(&ctx, &ff);
    zassert_equal(ctx.state, ISOTP_RECEIVING);

    /* Poll with time past N_Cr timeout */
    ISOTP_Poll(&ctx, ctx.rxLastFrameTime + 1001);

    zassert_equal(ctx.state, ISOTP_IDLE);
    zassert_false(ctx.rxComplete);
}

/** @brief No timeout while still within the N_Cr 1000 ms window — context stays ISOTP_RECEIVING. */
ZTEST(isotp_rx, test_ncr_no_timeout_within_window)
{
    uint8_t ff_data[] = {0x10, 14, 1, 2, 3, 4, 5, 6};
    DiveCANMessage_t ff = make_msg(TGT, SRC, ff_data, 8);
    (void)ISOTP_ProcessRxFrame(&ctx, &ff);

    /* Poll within timeout window */
    ISOTP_Poll(&ctx, ctx.rxLastFrameTime + 999);

    zassert_equal(ctx.state, ISOTP_RECEIVING);
}

/** @brief A 4-byte payload is sent as a DiveCAN SF: PCI byte includes padding length, byte[1]=0x00. */
ZTEST(isotp_tx, test_sf_with_padding)
{
    uint8_t payload[] = {0x62, 0xF0, 0x00, 0x01};
    bool ok = ISOTP_Send(&ctx, payload, 4);
    zassert_true(ok);

    /* Poll to trigger StartNextTx */
    ISOTP_TxQueue_Poll(k_uptime_get_32());

    zassert_true(test_get_frame_count() >= 1);
    const DiveCANMessage_t *sf = test_get_last_frame();

    /* DiveCAN SF: [PCI+len_with_pad][pad=0x00][data...] */
    zassert_equal(sf->data[0], 5); /* 4 + 1 padding byte */
    zassert_equal(sf->data[1], 0x00); /* DiveCAN padding byte */
    zassert_equal(sf->data[2], 0x62);
    zassert_equal(sf->data[3], 0xF0);
    zassert_equal(sf->data[4], 0x00);
    zassert_equal(sf->data[5], 0x01);
}

/** @brief Maximum SF payload with DiveCAN padding (6 data bytes + 1 pad = 7 total) fits in one frame. */
ZTEST(isotp_tx, test_sf_max_with_padding)
{
    /* Max SF with DiveCAN padding = 6 bytes */
    uint8_t payload[] = {1, 2, 3, 4, 5, 6};
    bool ok = ISOTP_Send(&ctx, payload, 6);
    zassert_true(ok);

    ISOTP_TxQueue_Poll(k_uptime_get_32());

    const DiveCANMessage_t *sf = test_get_last_frame();
    zassert_equal(sf->data[0], 7); /* 6 + 1 */
    zassert_equal(sf->data[1], 0x00); /* padding */
    zassert_equal(sf->data[2], 1);
    zassert_equal(sf->data[7], 6);
}

/** @brief A 10-byte payload triggers multi-frame; FF length field includes the padding byte (11). */
ZTEST(isotp_tx, test_multiframe_ff_with_padding)
{
    /* 10 bytes > 6 → multi-frame */
    uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    bool ok = ISOTP_Send(&ctx, payload, 10);
    zassert_true(ok);

    ISOTP_TxQueue_Poll(k_uptime_get_32());

    /* FF should be sent */
    zassert_true(test_get_frame_count() >= 1);
    const DiveCANMessage_t *ff = test_get_frame(0);

    /* DiveCAN FF: [PCI_hi][len_lo][pad=0x00][5 data bytes]
     * Length field = payload + 1 (padding) = 11 = 0x00B */
    zassert_equal(ff->data[0] & 0xF0, 0x10); /* FF PCI */
    uint16_t len = ((uint16_t)(ff->data[0] & 0x0F) << 8) | ff->data[1];
    zassert_equal(len, 11); /* 10 + 1 padding */
    zassert_equal(ff->data[2], 0x00); /* DiveCAN padding */
    zassert_equal(ff->data[3], 1); /* first data byte */
    zassert_equal(ff->data[7], 5); /* 5th data byte */
}

/** @brief After FF, a CTS Flow Control triggers the remaining CFs; total = FF + 1 CF for 10 bytes. */
ZTEST(isotp_tx, test_multiframe_cf_after_fc)
{
    uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    bool ok = ISOTP_Send(&ctx, payload, 10);
    zassert_true(ok);

    ISOTP_TxQueue_Poll(k_uptime_get_32());
    int ff_count = test_get_frame_count();
    zassert_equal(ff_count, 1); /* Only FF, waiting for FC */

    /* Send FC CTS (BS=0, STmin=0) */
    uint8_t fc_data[] = {0x30, 0x00, 0x00};
    DiveCANMessage_t fc = {
        .id = MSG_ID | ((uint32_t)SRC << 8) | (uint32_t)TGT,
        .length = 3,
    };
    (void)memcpy(fc.data, fc_data, 3);

    bool fc_consumed = ISOTP_TxQueue_ProcessFC(&fc);
    zassert_true(fc_consumed);

    /* CFs should have been sent. FF had 5 data bytes, need 5 more in 1 CF */
    int total_frames = test_get_frame_count();
    zassert_equal(total_frames, 2); /* FF + 1 CF */

    const DiveCANMessage_t *cf = test_get_frame(1);
    zassert_equal(cf->data[0] & 0xF0, 0x20); /* CF PCI */
    zassert_equal(cf->data[0] & 0x0F, 1); /* seq=1 */
    zassert_equal(cf->data[1], 6); /* continuing from byte 6 */
    zassert_equal(cf->data[5], 10);
}

/** @brief N_Bs timeout: TX queue clears itself if no FC arrives within 1000 ms after FF. */
ZTEST(isotp_tx, test_nbs_timeout)
{
    uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    (void)ISOTP_Send(&ctx, payload, 10);

    ISOTP_TxQueue_Poll(k_uptime_get_32());
    zassert_true(ISOTP_TxQueue_IsBusy());

    /* Wait past N_Bs timeout (1000ms), then poll */
    k_msleep(1100);
    ISOTP_TxQueue_Poll(k_uptime_get_32());
    zassert_false(ISOTP_TxQueue_IsBusy());
}

/** @brief Two queued payloads are sent in order: first poll sends p1, second poll sends p2. */
ZTEST(isotp_tx, test_queue_serialization)
{
    uint8_t p1[] = {0xAA, 0xBB};
    uint8_t p2[] = {0xCC, 0xDD};

    (void)ISOTP_Send(&ctx, p1, 2);
    (void)ISOTP_Send(&ctx, p2, 2);

    /* First poll sends p1 as SF */
    ISOTP_TxQueue_Poll(k_uptime_get_32());
    zassert_equal(test_get_frame_count(), 1);
    zassert_equal(test_get_frame(0)->data[2], 0xAA);

    /* Second poll sends p2 as SF */
    ISOTP_TxQueue_Poll(k_uptime_get_32());
    zassert_equal(test_get_frame_count(), 2);
    zassert_equal(test_get_frame(1)->data[2], 0xCC);
}

/** @brief A Flow Control OVFLW frame causes the TX queue to abort the in-progress transfer. */
ZTEST(isotp_tx, test_fc_ovflw_aborts)
{
    uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    (void)ISOTP_Send(&ctx, payload, 10);
    ISOTP_TxQueue_Poll(k_uptime_get_32());
    zassert_true(ISOTP_TxQueue_IsBusy());

    /* Send FC OVFLW */
    uint8_t fc_data[] = {0x32, 0x00, 0x00};
    DiveCANMessage_t fc = {
        .id = MSG_ID | ((uint32_t)SRC << 8) | (uint32_t)TGT,
        .length = 3,
    };
    (void)memcpy(fc.data, fc_data, 3);

    (void)ISOTP_TxQueue_ProcessFC(&fc);
    zassert_false(ISOTP_TxQueue_IsBusy());
}

/** @brief Block size (BS) in FC limits CFs per window; a second FC is needed to send the final CF. */
ZTEST(isotp_tx, test_block_size_handling)
{
    /* 20 bytes: FF=5, need 15 more = 3 CFs (7+7+1) */
    uint8_t payload[20];
    for (int i = 0; i < 20; i++) {
        payload[i] = (uint8_t)(i + 1);
    }

    (void)ISOTP_Send(&ctx, payload, 20);
    ISOTP_TxQueue_Poll(k_uptime_get_32());
    zassert_equal(test_get_frame_count(), 1); /* FF only */

    /* FC with BS=2: send 2 CFs then wait for another FC */
    uint8_t fc1_data[] = {0x30, 0x02, 0x00};
    DiveCANMessage_t fc1 = {
        .id = MSG_ID | ((uint32_t)SRC << 8) | (uint32_t)TGT,
        .length = 3,
    };
    (void)memcpy(fc1.data, fc1_data, 3);
    (void)ISOTP_TxQueue_ProcessFC(&fc1);

    /* Should have sent FF + 2 CFs = 3 frames, then waiting for FC again */
    zassert_equal(test_get_frame_count(), 3);
    zassert_true(ISOTP_TxQueue_IsBusy());

    /* Send another FC to get the last CF */
    DiveCANMessage_t fc2 = fc1;
    (void)ISOTP_TxQueue_ProcessFC(&fc2);

    /* Should have sent the last CF */
    zassert_equal(test_get_frame_count(), 4); /* FF + 3 CFs */
    zassert_false(ISOTP_TxQueue_IsBusy());
}
