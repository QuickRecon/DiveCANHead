/**
 * @file uds_ota.h
 * @brief UDS over-the-air firmware update service handler.
 *
 * Implements ISO 14229 services 0x34 (RequestDownload), 0x36 (TransferData),
 * 0x37 (RequestTransferExit), and 0x31 (RoutineControl, RID 0xF001 Activate).
 *
 * The OTA pipeline streams a signed MCUBoot image into slot1 over ISO-TP,
 * verifies the header at 0x37, then verifies the full SHA-256 hash from the
 * image's TLV trailer at 0x31 Activate before calling boot_request_upgrade()
 * and rebooting into the new image in MCUBoot's "test" mode.
 *
 * All OTA-related services require the programming session (UDS SID 0x10
 * subfunction 0x02) and refuse if the unit is in a dive (ambient pressure
 * above DIVE_AMBIENT_PRESSURE_THRESHOLD_MBAR).
 */
#ifndef UDS_OTA_H
#define UDS_OTA_H

#include <stdint.h>

#include "uds.h"

/**
 * @brief Dispatch an inbound OTA service request (SIDs 0x34/0x36/0x37/0x31).
 *
 * Called from UDS_ProcessRequest's SID switch. Internally routes to the
 * per-SID handler based on requestData[UDS_SID_IDX].
 *
 * @param ctx           UDS context (carries session state)
 * @param requestData   Request bytes starting at the SID byte
 * @param requestLength Total byte count of requestData
 */
void UDS_OTA_Handle(UDSContext_t *ctx, const uint8_t *requestData,
            uint16_t requestLength);

/**
 * @brief Reset the OTA pipeline to idle state.
 *
 * Exposed for unit testing — production code never needs to reset state
 * because the SID handlers manage transitions themselves.
 */
void UDS_OTA_Reset(void);

#endif /* UDS_OTA_H */
