/**
 * @file uds.h
 * @brief UDS (Unified Diagnostic Services) service dispatcher for DiveCAN
 *
 * Implements ISO 14229-1 diagnostic services over ISO-TP transport.
 *
 * Supported Services:
 * - 0x22: ReadDataByIdentifier - Read state/settings data
 * - 0x2E: WriteDataByIdentifier - Write settings/control
 *
 * @note Uses ISO-TP for transport (see isotp.h)
 */

#ifndef UDS_H
#define UDS_H

#include <stdint.h>
#include <stdbool.h>
#include "isotp.h"

/* UDS Service IDs (SID) */
typedef enum {
	UDS_SID_READ_DATA_BY_ID = 0x22,
	UDS_SID_WRITE_DATA_BY_ID = 0x2E
} UDS_SID_t;

/* UDS Response SIDs (positive response = request + 0x40) */
enum {
	UDS_RESPONSE_SID_OFFSET = 0x40,
	UDS_SID_NEGATIVE_RESPONSE = 0x7F
};

/* UDS Negative Response Codes (NRC) */
typedef enum {
	UDS_NRC_SERVICE_NOT_SUPPORTED = 0x11,
	UDS_NRC_INCORRECT_MSG_LEN = 0x13,
	UDS_NRC_RESPONSE_TOO_LONG = 0x14,
	UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22,
	UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31,
	UDS_NRC_GENERAL_PROG_FAIL = 0x72
} UDS_NRC_t;

/* UDS Data Identifiers (DID) - custom for DiveCAN */
typedef enum {
	UDS_DID_FIRMWARE_VERSION = 0xF000,
	UDS_DID_HARDWARE_VERSION = 0xF001,
	UDS_DID_LOG_MESSAGE = 0xA100
} UDS_DID_t;

/* UDS maximum message sizes */
enum {
	UDS_MAX_REQUEST_LENGTH = 128,
	UDS_MAX_RESPONSE_LENGTH = 128
};

/* UDS message byte positions
 * Request format: [pad][SID][DID_hi][DID_lo][data...] */
static const size_t UDS_PAD_IDX = 0U;
static const size_t UDS_SID_IDX = 1U;
static const size_t UDS_DID_HI_IDX = 2U;
static const size_t UDS_DID_LO_IDX = 3U;
static const size_t UDS_DATA_IDX = 4U;

/* UDS response structure sizes */
static const size_t UDS_NEG_RESP_LEN = 3U;
static const size_t UDS_POS_RESP_HDR = 3U;
static const size_t UDS_MIN_REQ_LEN = 4U;
static const size_t UDS_DID_SIZE = 2U;

/* Setting value response length: max value (uint64) + current value (uint64) */
#define SETTING_VALUE_RESP_LEN (2U * sizeof(uint64_t))

/* Settings DID range limits */
static const uint16_t UDS_DID_SETTING_LABEL_END = 0x9200U;
static const size_t SETTING_LABEL_MAX_LEN = 9U;

/**
 * @brief UDS context
 */
typedef struct {
	uint8_t responseBuffer[UDS_MAX_RESPONSE_LENGTH];
	uint16_t responseLength;
	ISOTPContext_t *isotpContext;
} UDSContext_t;

void UDS_Init(UDSContext_t *ctx, ISOTPContext_t *isotpCtx);
void UDS_ProcessRequest(UDSContext_t *ctx, const uint8_t *requestData,
			uint16_t requestLength);
void UDS_SendNegativeResponse(UDSContext_t *ctx, uint8_t requestedSID,
			      uint8_t nrc);
void UDS_SendResponse(UDSContext_t *ctx);

#endif /* UDS_H */
