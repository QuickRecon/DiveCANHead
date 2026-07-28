/**
 * @file divecan_counters.h
 * @brief F-section instrumentation counters (CAN TX, BUS_INIT/BUS_ID RX).
 *
 * Used by the firmware-confirm POST module (Phase 4) to verify that CAN
 * traffic is flowing and a handset is talking to us before the new image is
 * confirmed. Defined as F-section instrumentation so the counters can be
 * read independently of zbus channel state.
 *
 * Counters saturate at UINT32_MAX rather than wrap, so the POST check can
 * use a simple "did the counter advance by N since I last looked" pattern
 * without worrying about wrap-around during the deadline window.
 */
#ifndef DIVECAN_COUNTERS_H
#define DIVECAN_COUNTERS_H

#include <stdint.h>

/**
 * @brief Number of DiveCAN frames successfully handed to the CAN driver.
 *
 * Bumped on every divecan_send()/divecan_send_blocking() that returns 0.
 * Saturating; never wraps.
 */
uint32_t divecan_send_get_tx_count(void);

/**
 * @brief Number of BUS_INIT_ID frames received on the bus.
 *
 * Bumped from the RX thread's dispatch switch. Used by POST to confirm a
 * handset is present.
 */
uint32_t divecan_rx_get_bus_init_count(void);

/**
 * @brief Number of BUS_ID_ID (ping) frames received on the bus.
 *
 * Bumped from the RX thread's dispatch switch. Used together with the
 * BUS_INIT count to confirm handset presence (a fresh handset issues
 * BUS_INIT first, an already-paired handset just pings).
 */
uint32_t divecan_rx_get_bus_id_count(void);

#endif /* DIVECAN_COUNTERS_H */
