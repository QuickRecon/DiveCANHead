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

/** @brief A valid 3-byte SF is accepted, rx_complete is set, and bytes are copied to rx_buffer. */
ZTEST(isotp_rx, test_sf_basic)
{
    uint8_t data[] = {0x03, 0xAA, 0xBB, 0xCC};
    DiveCANMessage_t msg = make_msg(TGT, SRC, data, 4);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &msg);

    zassert_true(consumed);
    zassert_true(ctx.rx_complete);
    zassert_equal(ctx.rx_data_length, 3);
    zassert_equal(ctx.rx_buffer[0], 0xAA);
    zassert_equal(ctx.rx_buffer[1], 0xBB);
    zassert_equal(ctx.rx_buffer[2], 0xCC);
}

/** @brief Maximum SF payload (7 bytes — the ISO-TP limit for a CAN SF) is fully received. */
ZTEST(isotp_rx, test_sf_max_length)
{
    uint8_t data[] = {0x07, 1, 2, 3, 4, 5, 6, 7};
    DiveCANMessage_t msg = make_msg(TGT, SRC, data, 8);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &msg);

    zassert_true(consumed);
    zassert_true(ctx.rx_complete);
    zassert_equal(ctx.rx_data_length, 7);
    for (int i = 0; i < 7; i++) {
        zassert_equal(ctx.rx_buffer[i], (uint8_t)(i + 1));
    }
}

/** @brief An SF with a zero length field is rejected (not consumed, rx_complete stays false). */
ZTEST(isotp_rx, test_sf_zero_length_rejected)
{
    uint8_t data[] = {0x00, 0xAA};
    DiveCANMessage_t msg = make_msg(TGT, SRC, data, 2);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &msg);

    zassert_false(consumed);
    zassert_false(ctx.rx_complete);
}

/** @brief A frame addressed to a different target is silently ignored (not consumed). */
ZTEST(isotp_rx, test_sf_wrong_target_ignored)
{
    uint8_t data[] = {0x03, 0xAA, 0xBB, 0xCC};
    DiveCANMessage_t msg = make_msg(TGT, DIVECAN_MONITOR, data, 4);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &msg);

    zassert_false(consumed);
    zassert_false(ctx.rx_complete);
}

/** @brief FF followed by one CF reassembles 10 bytes correctly; a FC is sent after the FF. */
ZTEST(isotp_rx, test_multiframe_reassembly)
{
    /* 10 bytes total: FF carries 6, CF carries remaining 4 */
    uint8_t ff_data[] = {0x10, 10, 1, 2, 3, 4, 5, 6};
    DiveCANMessage_t ff = make_msg(TGT, SRC, ff_data, 8);

    bool consumed = ISOTP_ProcessRxFrame(&ctx, &ff);
    zassert_true(consumed);
    zassert_false(ctx.rx_complete);
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
    zassert_true(ctx.rx_complete);
    zassert_equal(ctx.rx_data_length, 10);

    for (int i = 0; i < 10; i++) {
        zassert_equal(ctx.rx_buffer[i], (uint8_t)(i + 1),
                  "byte %d: expected %d got %d", i, i + 1,
                  ctx.rx_buffer[i]);
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
    zassert_false(ctx.rx_complete);

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
    zassert_false(ctx.rx_complete);
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

/**
 * @brief A dialog context must retarget back to the handset after the BT bridge (0xFF) talks.
 *
 * Regression for the "Bus Devices menu dies after a BT ISO-TP interaction" bug:
 * the BT bridge sources every frame from 0xFF, so a dialog request from it
 * retargets the shared context to 0xFF. The context must still retarget back to
 * the handset on its next frame — otherwise menu replies are stranded on 0xFF
 * and the handset's Bus Devices menu stays dead until the head reboots.
 */
ZTEST(isotp_rx, test_dialog_retargets_off_broadcast)
{
    /* ctx is a dialog context (target TGT, broadcast_tx == false). */
    zassert_false(ctx.broadcast_tx, "default dialog context must not be broadcast");

    /* BT bridge (source 0xFF) sends an SF addressed to us -> retarget to 0xFF. */
    uint8_t data[] = {0x03, 0xAA, 0xBB, 0xCC};
    DiveCANMessage_t bt = make_msg(0xFF, SRC, data, 4);
    zassert_true(ISOTP_ProcessRxFrame(&ctx, &bt));
    zassert_equal((uint8_t)ctx.target, 0xFF, "must retarget to the BT bridge source");

    /* Handset (source TGT) sends its next frame -> must retarget back to TGT. */
    DiveCANMessage_t hs = make_msg(TGT, SRC, data, 4);
    zassert_true(ISOTP_ProcessRxFrame(&ctx, &hs));
    zassert_equal((uint8_t)ctx.target, (uint8_t)TGT,
              "dialog context must retarget off 0xFF back to the handset");
}

/**
 * @brief A broadcast-role context (log-push) must NEVER retarget to a unicast sender.
 *
 * Complements test_dialog_retargets_off_broadcast: the role flag, not the live
 * target, is what locks retargeting. A context created with the broadcast target
 * keeps target == 0xFF even when a unicast peer sends it a frame.
 */
ZTEST(isotp_rx, test_broadcast_context_never_retargets)
{
    ISOTP_Init(&ctx, SRC, (DiveCANType_t)0xFF, MSG_ID);
    zassert_true(ctx.broadcast_tx, "context created with 0xFF target is broadcast");

    uint8_t data[] = {0x03, 0xAA, 0xBB, 0xCC};
    DiveCANMessage_t hs = make_msg(TGT, SRC, data, 4);
    (void)ISOTP_ProcessRxFrame(&ctx, &hs);
    zassert_equal((uint8_t)ctx.target, 0xFF,
              "broadcast context must not retarget to a unicast sender");
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
    ISOTP_Poll(&ctx, ctx.rx_last_frame_time + 1001);

    zassert_equal(ctx.state, ISOTP_IDLE);
    zassert_false(ctx.rx_complete);
}

/** @brief No timeout while still within the N_Cr 1000 ms window — context stays ISOTP_RECEIVING. */
ZTEST(isotp_rx, test_ncr_no_timeout_within_window)
{
    uint8_t ff_data[] = {0x10, 14, 1, 2, 3, 4, 5, 6};
    DiveCANMessage_t ff = make_msg(TGT, SRC, ff_data, 8);
    (void)ISOTP_ProcessRxFrame(&ctx, &ff);

    /* Poll within timeout window */
    ISOTP_Poll(&ctx, ctx.rx_last_frame_time + 999);

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
    uint16_t len = (uint16_t)((uint16_t)((uint16_t)(ff->data[0] & 0x0FU) << 8U) | ff->data[1]);
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

/* ---- Broadcast / log-push TX contract ----
 *
 * A broadcast transfer (target == ISOTP_BROADCAST_ADDR, 0xFF) can NEVER receive
 * a Flow Control frame — nobody is addressed to answer it. The log-push stream
 * uses exactly this addressing. Yet the TX state machine currently parks such a
 * multi-frame transfer in WAIT_FC until the N_Bs (1 s) timeout, head-of-line
 * blocking any UDS dialog reply queued behind it (the ISO-TP flakiness saga).
 *
 * The intended architecture is "fire-and-forget" for broadcast: after the FF,
 * stream all CFs (implicit CTS, BS=0) straight back to IDLE, never entering
 * WAIT_FC. These tests pin that contract. Until the Phase-2 refactor lands the
 * first one is EXPECTED RED (only the FF is sent, the SM stays busy). */

/** @brief Reconfigure the shared context as the broadcast (log-push) sender.
 *
 * The broadcast traffic class is identified purely by the target address
 * (0xFF) — the TX queue sends it fire-and-forget. No per-context flag. */
static void make_broadcast_ctx(void)
{
    ISOTP_Init(&ctx, SRC, (DiveCANType_t)0xFF, MSG_ID);
}

/** @brief Scan captured frames for an SF addressed to `target` carrying `firstByte` at data[2]. */
static bool captured_sf_to(uint8_t target, uint8_t firstByte)
{
    for (int i = 0; i < test_get_frame_count(); i++) {
        const DiveCANMessage_t *f = test_get_frame(i);
        uint8_t fTarget = (uint8_t)((f->id >> 8) & 0x0F);
        bool isSF = (f->data[0] & 0xF0) == 0x00; /* SF PCI high nibble */
        if ((fTarget == (target & 0x0F)) && isSF && (f->data[2] == firstByte)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Broadcast multi-frame is fire-and-forget: one poll sends FF + all CFs and returns to IDLE.
 *
 * EXPECTED RED until the Phase-2 broadcast refactor — today the SM sends only
 * the FF and parks in WAIT_FC awaiting an FC that a broadcast can never receive.
 */
ZTEST(isotp_tx, test_broadcast_multiframe_fire_and_forget)
{
    make_broadcast_ctx();

    /* 10 bytes > 6 → multi-frame: FF carries 5, one CF carries the remaining 5. */
    uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    bool ok = ISOTP_Send(&ctx, payload, 10);
    zassert_true(ok);

    ISOTP_TxQueue_Poll(k_uptime_get_32());

    /* A broadcast transfer cannot wait for FC — the whole message must be on the
     * wire after a single poll, and the queue must be idle (ready for the next
     * message) without any 1 s N_Bs stall. */
    zassert_equal(test_get_frame_count(), 2,
              "broadcast multi-frame must emit FF + CF in one poll (got %d)",
              test_get_frame_count());
    zassert_false(ISOTP_TxQueue_IsBusy(),
              "broadcast transfer must not park in WAIT_FC");
}

/**
 * @brief A broadcast (log-push) transfer must not stall a UDS dialog reply queued behind it.
 *
 * Implementation-agnostic contract: after a broadcast multi-frame is in progress
 * and a dialog SF is enqueued, polling (with NO time advance, so the N_Bs timeout
 * cannot have fired) must still get the dialog SF onto the wire. If the dialog
 * were head-of-line blocked behind the broadcast's WAIT_FC it would not appear
 * until 1 s elapsed. Guards the Phase-2 removal of the preemption patches.
 */
ZTEST(isotp_tx, test_broadcast_does_not_stall_dialog)
{
    /* Start a broadcast multi-frame transfer. */
    make_broadcast_ctx();
    uint8_t bcast[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    (void)ISOTP_Send(&ctx, bcast, 10);
    ISOTP_TxQueue_Poll(k_uptime_get_32());

    /* A real UDS dialog reply (addressed, non-preemptible) is now produced. */
    ISOTPContext_t dialog;
    ISOTP_Init(&dialog, SRC, TGT, MSG_ID); /* preemptible = false by default */
    uint8_t reply[] = {0xAA, 0xBB};
    (void)ISOTP_Send(&dialog, reply, 2);

    /* Drain WITHOUT advancing time — a stall would require the 1 s N_Bs timeout. */
    ISOTP_TxQueue_Poll(k_uptime_get_32());
    ISOTP_TxQueue_Poll(k_uptime_get_32());

    zassert_true(captured_sf_to((uint8_t)TGT, 0xAA),
             "dialog reply must reach the wire without waiting on the broadcast's FC");
    zassert_false(ISOTP_TxQueue_IsBusy());
}

/**
 * @brief Regression guard: ADDRESSED multi-frame transfers must still honour Flow Control.
 *
 * The broadcast fire-and-forget path must not leak into normal addressed UDS
 * transfers — those must still send only the FF and wait for an FC. Green now
 * and must stay green after the refactor.
 */
ZTEST(isotp_tx, test_addressed_multiframe_still_waits_fc)
{
    /* Default ctx is addressed (target == DIVECAN_CONTROLLER). */
    uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    (void)ISOTP_Send(&ctx, payload, 10);

    ISOTP_TxQueue_Poll(k_uptime_get_32());

    zassert_equal(test_get_frame_count(), 1, "addressed transfer sends only the FF before FC");
    zassert_true(ISOTP_TxQueue_IsBusy(), "addressed transfer must wait for FC");
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

ZTEST(isotp_rx, test_null_and_unknown_frame_inputs_are_rejected)
{
    uint8_t payload[] = {0x40U, 0xAAU}; /* reserved/unknown PCI type */
    DiveCANMessage_t unknown = make_msg(TGT, SRC, payload, sizeof(payload));

    ISOTP_Init(NULL, SRC, TGT, MSG_ID);
    ISOTP_Reset(NULL);
    ISOTP_Poll(NULL, 0U);
    zassert_false(ISOTP_ProcessRxFrame(NULL, &unknown));
    zassert_false(ISOTP_ProcessRxFrame(&ctx, NULL));
    zassert_false(ISOTP_ProcessRxFrame(&ctx, &unknown));
}

ZTEST(isotp_rx, test_short_single_frame_is_rejected)
{
    uint8_t payload[] = {0x03U, 0xAAU};
    DiveCANMessage_t short_sf = make_msg(TGT, SRC, payload, sizeof(payload));

    zassert_false(ISOTP_ProcessRxFrame(&ctx, &short_sf));
    zassert_false(ctx.rx_complete);
}

ZTEST(isotp_rx, test_zero_length_first_frame_is_rejected_with_overflow)
{
    uint8_t payload[] = {0x10U, 0x00U, 0U, 0U, 0U, 0U, 0U, 0U};
    DiveCANMessage_t ff = make_msg(TGT, SRC, payload, sizeof(payload));

    zassert_true(ISOTP_ProcessRxFrame(&ctx, &ff));
    zassert_equal(ctx.state, ISOTP_IDLE);
    zassert_false(ctx.rx_complete);
    zassert_not_null(test_get_last_frame());
    zassert_equal(test_get_last_frame()->data[0], ISOTP_FC_OVFLW);
}

ZTEST(isotp_rx, test_single_frame_restarts_an_active_reassembly)
{
    uint8_t ff_data[] = {0x10U, 14U, 1U, 2U, 3U, 4U, 5U, 6U};
    DiveCANMessage_t ff = make_msg(TGT, SRC, ff_data, sizeof(ff_data));
    zassert_true(ISOTP_ProcessRxFrame(&ctx, &ff));
    zassert_equal(ctx.state, ISOTP_RECEIVING);

    uint8_t sf_data[] = {0x02U, 0xAAU, 0xBBU};
    DiveCANMessage_t sf = make_msg(TGT, SRC, sf_data, sizeof(sf_data));
    zassert_true(ISOTP_ProcessRxFrame(&ctx, &sf));
    zassert_equal(ctx.state, ISOTP_IDLE);
    zassert_true(ctx.rx_complete);
    zassert_equal(ctx.rx_data_length, 2U);
    zassert_equal(ctx.rx_buffer[0], 0xAAU);
}

ZTEST(isotp_rx, test_first_frame_restarts_an_active_reassembly)
{
    uint8_t first_data[] = {0x10U, 14U, 1U, 2U, 3U, 4U, 5U, 6U};
    DiveCANMessage_t first = make_msg(TGT, SRC, first_data, sizeof(first_data));
    zassert_true(ISOTP_ProcessRxFrame(&ctx, &first));

    uint8_t replacement_data[] = {0x10U, 10U, 9U, 8U, 7U, 6U, 5U, 4U};
    DiveCANMessage_t replacement = make_msg(TGT, SRC, replacement_data,
                            sizeof(replacement_data));
    zassert_true(ISOTP_ProcessRxFrame(&ctx, &replacement));
    zassert_equal(ctx.state, ISOTP_RECEIVING);
    zassert_equal(ctx.rx_data_length, 10U);
    zassert_equal(ctx.rx_buffer[0], 9U);
}

ZTEST(isotp_tx, test_send_and_enqueue_validate_arguments)
{
    uint8_t payload = 0xAAU;

    zassert_false(ISOTP_Send(NULL, &payload, 1U));
    zassert_false(ISOTP_Send(&ctx, NULL, 1U));
    zassert_false(ISOTP_Send(&ctx, &payload, 0U));
    zassert_false(ISOTP_Send(&ctx, &payload, ISOTP_MAX_PAYLOAD + 1U));

    zassert_false(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID, NULL, 1U));
    zassert_false(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID, &payload, 0U));
    zassert_false(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID, &payload,
                       ISOTP_TX_BUFFER_SIZE + 1U));
}

ZTEST(isotp_tx, test_flow_control_validation_and_wait_abort)
{
    uint8_t payload[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};

    zassert_false(ISOTP_TxQueue_ProcessFC(NULL));

    uint8_t cts_data[] = {ISOTP_FC_CTS, 0U, 0U};
    DiveCANMessage_t cts = make_msg(TGT, SRC, cts_data, sizeof(cts_data));
    zassert_false(ISOTP_TxQueue_ProcessFC(&cts),
              "an FC while idle is spurious");

    zassert_true(ISOTP_Send(&ctx, payload, sizeof(payload)));
    ISOTP_TxQueue_Poll(k_uptime_get_32());

    DiveCANMessage_t wrong_target = make_msg(TGT, DIVECAN_MONITOR,
                             cts_data, sizeof(cts_data));
    zassert_false(ISOTP_TxQueue_ProcessFC(&wrong_target));
    zassert_true(ISOTP_TxQueue_IsBusy());

    uint8_t wait_data[] = {ISOTP_FC_WAIT, 0U, 0U};
    DiveCANMessage_t wait = make_msg(TGT, SRC, wait_data, sizeof(wait_data));
    zassert_true(ISOTP_TxQueue_ProcessFC(&wait));
    zassert_false(ISOTP_TxQueue_IsBusy());
}

ZTEST(isotp_tx, test_unknown_flow_control_status_aborts)
{
    uint8_t payload[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    zassert_true(ISOTP_Send(&ctx, payload, sizeof(payload)));
    ISOTP_TxQueue_Poll(k_uptime_get_32());

    uint8_t bad_data[] = {0x33U, 0U, 0U};
    DiveCANMessage_t bad = make_msg(TGT, SRC, bad_data, sizeof(bad_data));
    zassert_true(ISOTP_TxQueue_ProcessFC(&bad));
    zassert_false(ISOTP_TxQueue_IsBusy());
}

ZTEST(isotp_tx, test_flow_control_stmin_is_honoured)
{
    uint8_t payload[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    zassert_true(ISOTP_Send(&ctx, payload, sizeof(payload)));
    ISOTP_TxQueue_Poll(k_uptime_get_32());

    uint8_t fc_data[] = {ISOTP_FC_CTS, 0U, 1U};
    DiveCANMessage_t fc = make_msg(TGT, SRC, fc_data, sizeof(fc_data));
    zassert_true(ISOTP_TxQueue_ProcessFC(&fc));
    zassert_false(ISOTP_TxQueue_IsBusy());
    zassert_equal(test_get_frame_count(), 2);
}

ZTEST(isotp_tx, test_queue_full_is_reported_while_transfer_active)
{
    uint8_t long_payload[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    uint8_t short_payload[] = {0xAAU};

    zassert_true(ISOTP_Send(&ctx, long_payload, sizeof(long_payload)));
    ISOTP_TxQueue_Poll(k_uptime_get_32());
    zassert_true(ISOTP_TxQueue_IsBusy());

    zassert_true(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID,
                      short_payload, sizeof(short_payload)));
    zassert_true(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID,
                      short_payload, sizeof(short_payload)));
    zassert_equal(ISOTP_TxQueue_GetPendingCount(), ISOTP_TX_QUEUE_SIZE);
    zassert_false(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID,
                       short_payload, sizeof(short_payload)));
}

ZTEST(isotp_tx, test_queue_full_recovers_after_flow_control_timeout)
{
    uint8_t long_payload[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    uint8_t short_payload[] = {0xAAU};

    zassert_true(ISOTP_Send(&ctx, long_payload, sizeof(long_payload)));
    ISOTP_TxQueue_Poll(k_uptime_get_32());
    zassert_true(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID,
                      short_payload, sizeof(short_payload)));
    zassert_true(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID,
                      short_payload, sizeof(short_payload)));

    k_msleep(ISOTP_TIMEOUT_N_BS + 1U);
    zassert_true(ISOTP_TxQueue_Enqueue(SRC, TGT, MSG_ID,
                      short_payload, sizeof(short_payload)),
             "a stale in-flight transfer should be reaped to make progress");
    zassert_false(ISOTP_TxQueue_IsBusy());
    zassert_equal(ISOTP_TxQueue_GetPendingCount(), ISOTP_TX_QUEUE_SIZE);
}
