/**
 * @file uds.c
 * @brief UDS service dispatcher implementation
 *
 * Implements UDS diagnostic services over ISO-TP transport.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <string.h>

#include "uds.h"
#include "uds_settings.h"
#include "uds_state_did.h"
#include "divecan_channels.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "calibration.h"
#include "errors.h"

LOG_MODULE_REGISTER(uds, LOG_LEVEL_INF);

/* Setting info field sizes */
static const uint16_t SETTING_INFO_BASE_LEN = 3U;   /* null + kind + editable */
static const uint16_t SETTING_INFO_TEXT_EXTRA = 2U; /* maxValue + optionCount */

/* Setting info field offsets from label end */
static const size_t SI_NULL_OFF = 0U;
static const size_t SI_KIND_OFF = 1U;
static const size_t SI_EDIT_OFF = 2U;
static const size_t SI_MAX_OFF = 3U;
static const size_t SI_COUNT_OFF = 4U;

/* UDS write message lengths */
static const uint16_t UDS_SINGLE_VALUE_LEN = 5U;
static const uint16_t SETTING_VALUE_WRITE_LEN = 12U;

/* Forward declarations */
static void HandleReadDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleWriteDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static bool readSettingInfoDID(uint16_t did, uint8_t *buf, uint16_t dataOffset, uint16_t maxAvailable, uint16_t *bytesWritten);
static bool readSettingValueDID(uint16_t did, uint8_t *buf, uint16_t dataOffset, uint16_t maxAvailable, uint16_t *bytesWritten);
static bool readSettingLabelDID(uint16_t did, uint8_t *buf, uint16_t dataOffset, uint16_t maxAvailable, uint16_t *bytesWritten);
static bool writeSetpointDID(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static bool writeCalibrationTriggerDID(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static bool writeSettingSaveDID(UDSContext_t *ctx, uint16_t did, const uint8_t *requestData, uint16_t requestLength);
static bool writeSettingValueDID_handler(UDSContext_t *ctx, uint16_t did, const uint8_t *requestData, uint16_t requestLength);

/**
 * @brief Initialize UDS context
 */
void UDS_Init(UDSContext_t *ctx, ISOTPContext_t *isotpCtx)
{
	if (ctx == NULL) {
		OP_ERROR(OP_ERR_NULL_PTR);
	} else {
		(void)memset(ctx, 0, sizeof(UDSContext_t));
		ctx->isotpContext = isotpCtx;
	}
}

/**
 * @brief Process UDS request message
 */
void UDS_ProcessRequest(UDSContext_t *ctx, const uint8_t *requestData,
			uint16_t requestLength)
{
	if ((ctx == NULL) || (requestData == NULL) || (requestLength == 0)) {
		OP_ERROR(OP_ERR_NULL_PTR);
	} else {
		uint8_t sid = requestData[UDS_SID_IDX];

		switch (sid) {
		case UDS_SID_READ_DATA_BY_ID:
			HandleReadDataByIdentifier(ctx, requestData, requestLength);
			break;

		case UDS_SID_WRITE_DATA_BY_ID:
			HandleWriteDataByIdentifier(ctx, requestData, requestLength);
			break;

		default:
			OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_SUPPORTED);
			UDS_SendNegativeResponse(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
			break;
		}
	}
}

/**
 * @brief Send UDS negative response
 */
void UDS_SendNegativeResponse(UDSContext_t *ctx, uint8_t requestedSID,
			      uint8_t nrc)
{
	if ((ctx == NULL) || (ctx->isotpContext == NULL)) {
		OP_ERROR(OP_ERR_NULL_PTR);
	} else {
		ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_NEGATIVE_RESPONSE;
		ctx->responseBuffer[UDS_SID_IDX] = requestedSID;
		ctx->responseBuffer[UDS_DID_HI_IDX] = nrc;
		ctx->responseLength = UDS_NEG_RESP_LEN;

		(void)ISOTP_Send(ctx->isotpContext, ctx->responseBuffer,
				 ctx->responseLength);
	}
}

/**
 * @brief Send UDS positive response
 */
void UDS_SendResponse(UDSContext_t *ctx)
{
	if ((ctx == NULL) || (ctx->isotpContext == NULL) ||
	    (ctx->responseLength == 0)) {
		OP_ERROR(OP_ERR_NULL_PTR);
	} else {
		(void)ISOTP_Send(ctx->isotpContext, ctx->responseBuffer,
				 ctx->responseLength);
	}
}

/* ---- DID read helpers ---- */

static const char *getCommitHash(void)
{
#ifdef STRINGIFY
#undef STRINGIFY
#endif
#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)
#ifdef APP_BUILD_VERSION
	return STRINGIFY(APP_BUILD_VERSION);
#else
	return "dev";
#endif
}

/**
 * @brief Read a single DID and append to response buffer
 */
static bool ReadSingleDID(UDSContext_t *ctx, uint16_t did,
			   uint16_t responseOffset, uint16_t *bytesWritten)
{
	bool result = false;
	uint8_t *buf = &ctx->responseBuffer[responseOffset];
	uint16_t maxAvailable = UDS_MAX_RESPONSE_LENGTH - responseOffset;
	*bytesWritten = 0U;

	if (maxAvailable < (UDS_DID_SIZE + 1U)) {
		OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, did);
	} else {
		/* Write DID header (big-endian) */
		buf[0] = (uint8_t)(did >> DIVECAN_BYTE_WIDTH);
		buf[1] = (uint8_t)(did);
		uint16_t dataOffset = UDS_DID_SIZE;

		/* Try state DID handler first (0xF2xx, 0xF4xx) */
		if (UDS_StateDID_IsStateDID(did)) {
			uint16_t dataLen = 0U;
			if (UDS_StateDID_HandleRead(did, &buf[dataOffset], &dataLen)) {
				*bytesWritten = dataOffset + dataLen;
				result = true;
			}
		} else if (did == UDS_DID_FIRMWARE_VERSION) {
			const char *commitHash = getCommitHash();
			uint16_t hashLen = (uint16_t)strnlen(commitHash, 10);
			if (hashLen > (maxAvailable - dataOffset)) {
				hashLen = maxAvailable - dataOffset;
			}
			(void)memcpy(&buf[dataOffset], commitHash, hashLen);
			*bytesWritten = dataOffset + hashLen;
			result = true;
		} else if (did == UDS_DID_HARDWARE_VERSION) {
			buf[dataOffset] = 0; /* TODO: wire to hw_version driver */
			*bytesWritten = dataOffset + 1U;
			result = true;
		} else if (did == UDS_DID_SETTING_COUNT) {
			buf[dataOffset] = UDS_GetSettingCount();
			*bytesWritten = dataOffset + 1U;
			result = true;
		} else if ((did >= UDS_DID_SETTING_INFO_BASE) &&
			   (did < (UDS_DID_SETTING_INFO_BASE + UDS_GetSettingCount()))) {
			result = readSettingInfoDID(did, buf, dataOffset, maxAvailable, bytesWritten);
		} else if ((did >= UDS_DID_SETTING_VALUE_BASE) &&
			   (did < (UDS_DID_SETTING_VALUE_BASE + UDS_GetSettingCount()))) {
			result = readSettingValueDID(did, buf, dataOffset, maxAvailable, bytesWritten);
		} else if ((did >= UDS_DID_SETTING_LABEL_BASE) &&
			   (did < UDS_DID_SETTING_LABEL_END)) {
			result = readSettingLabelDID(did, buf, dataOffset, maxAvailable, bytesWritten);
		} else {
			/* DID not found */
		}
	}

	return result;
}

/**
 * @brief Handle ReadDataByIdentifier (0x22)
 */
static void HandleReadDataByIdentifier(UDSContext_t *ctx,
				       const uint8_t *requestData,
				       uint16_t requestLength)
{
	if (requestLength < UDS_MIN_REQ_LEN) {
		OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
		UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
	} else if (((requestLength - UDS_DID_SIZE) % UDS_DID_SIZE) != 0U) {
		OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
		UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
	} else {
		ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_READ_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
		uint16_t responseOffset = UDS_SID_IDX;
		bool processingOk = true;

		uint16_t requestOffset = UDS_DID_HI_IDX;
		while (processingOk && ((requestOffset + UDS_DID_SIZE) <= requestLength)) {
			uint16_t did = (uint16_t)((uint16_t)requestData[requestOffset] << DIVECAN_BYTE_WIDTH) |
				       (uint16_t)requestData[requestOffset + 1U];
			requestOffset += UDS_DID_SIZE;

			uint16_t bytesWritten = 0U;
			if (!ReadSingleDID(ctx, did, responseOffset, &bytesWritten)) {
				OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
				UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
				processingOk = false;
			} else {
				responseOffset += bytesWritten;
				if (responseOffset >= UDS_MAX_RESPONSE_LENGTH) {
					OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_RESPONSE_TOO_LONG);
					UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_RESPONSE_TOO_LONG);
					processingOk = false;
				}
			}
		}

		if (processingOk) {
			ctx->responseLength = responseOffset;
			UDS_SendResponse(ctx);
		}
	}
}

/* ---- Setting read helpers ---- */

static bool readSettingInfoDID(uint16_t did, uint8_t *buf,
			       uint16_t dataOffset, uint16_t maxAvailable,
			       uint16_t *bytesWritten)
{
	bool result = false;
	uint8_t index = (uint8_t)(did - UDS_DID_SETTING_INFO_BASE);
	const SettingDefinition_t *setting = UDS_GetSettingInfo(index);

	if (setting == NULL) {
		OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, index);
	} else {
		uint16_t labelLen = SETTING_LABEL_MAX_LEN;
		uint16_t needed = labelLen + SETTING_INFO_BASE_LEN;
		if (setting->kind == SETTING_KIND_TEXT) {
			needed += SETTING_INFO_TEXT_EXTRA;
		}

		if (needed > (maxAvailable - dataOffset)) {
			OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, index);
		} else {
			/* Label always 9 bytes, 0 padded */
			(void)memset(&buf[dataOffset], 0, labelLen);
			(void)memcpy(&buf[dataOffset], setting->label,
				     strnlen(setting->label, labelLen));
			buf[dataOffset + labelLen + SI_NULL_OFF] = 0;
			buf[dataOffset + labelLen + SI_KIND_OFF] = (uint8_t)setting->kind;
			if (setting->editable) {
				buf[dataOffset + labelLen + SI_EDIT_OFF] = 1U;
			} else {
				buf[dataOffset + labelLen + SI_EDIT_OFF] = 0U;
			}
			if (setting->kind == SETTING_KIND_TEXT) {
				buf[dataOffset + labelLen + SI_MAX_OFF] = setting->maxValue;
				buf[dataOffset + labelLen + SI_COUNT_OFF] = setting->optionCount;
				*bytesWritten = dataOffset + labelLen + SETTING_INFO_BASE_LEN + SETTING_INFO_TEXT_EXTRA;
			} else {
				*bytesWritten = dataOffset + labelLen + SETTING_INFO_BASE_LEN;
			}
			result = true;
		}
	}

	return result;
}

static bool readSettingValueDID(uint16_t did, uint8_t *buf,
				uint16_t dataOffset, uint16_t maxAvailable,
				uint16_t *bytesWritten)
{
	bool result = false;
	uint8_t index = (uint8_t)(did - UDS_DID_SETTING_VALUE_BASE);
	const SettingDefinition_t *setting = UDS_GetSettingInfo(index);

	if (setting == NULL) {
		OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, index);
	} else if (maxAvailable < (dataOffset + SETTING_VALUE_RESP_LEN)) {
		OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, index);
	} else {
		uint64_t maxValue = setting->maxValue;
		uint64_t currentValue = UDS_GetSettingValue(index);

		for (uint8_t i = 0; i < sizeof(uint64_t); ++i) {
			buf[dataOffset + i] = (uint8_t)(maxValue >> (DIVECAN_SEVEN_BYTE_WIDTH - (i * DIVECAN_BYTE_WIDTH)));
			buf[dataOffset + sizeof(uint64_t) + i] = (uint8_t)(currentValue >> (DIVECAN_SEVEN_BYTE_WIDTH - (i * DIVECAN_BYTE_WIDTH)));
		}
		*bytesWritten = dataOffset + SETTING_VALUE_RESP_LEN;
		result = true;
	}

	return result;
}

static bool readSettingLabelDID(uint16_t did, uint8_t *buf,
				uint16_t dataOffset, uint16_t maxAvailable,
				uint16_t *bytesWritten)
{
	bool result = false;
	uint16_t offset = did - UDS_DID_SETTING_LABEL_BASE;
	uint8_t settingIndex = (uint8_t)(offset & ISOTP_SEQ_MASK);
	uint8_t optionIndex = (uint8_t)((offset >> DIVECAN_HALF_BYTE_WIDTH) & ISOTP_SEQ_MASK);

	const char *label = UDS_GetSettingOptionLabel(settingIndex, optionIndex);
	if (label == NULL) {
		OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, did);
	} else {
		uint16_t optLabelLen = (uint16_t)strnlen(label, SETTING_LABEL_MAX_LEN);
		if (optLabelLen > (maxAvailable - dataOffset - 1U)) {
			optLabelLen = maxAvailable - dataOffset - 1U;
		}
		(void)memcpy(&buf[dataOffset], label, optLabelLen);
		buf[dataOffset + optLabelLen] = 0;
		*bytesWritten = dataOffset + optLabelLen + 1U;
		result = true;
	}

	return result;
}

/* ---- Write handlers ---- */

static bool writeSetpointDID(UDSContext_t *ctx, const uint8_t *requestData,
			     uint16_t requestLength)
{
	if (requestLength != UDS_SINGLE_VALUE_LEN) {
		OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
		UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
	} else {
		PPO2_t ppo2 = requestData[UDS_DATA_IDX];
		(void)zbus_chan_pub(&chan_setpoint, &ppo2, K_MSEC(100));

		ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
		ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
		ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
		ctx->responseLength = UDS_POS_RESP_HDR;
		UDS_SendResponse(ctx);
	}

	return true;
}

static bool writeCalibrationTriggerDID(UDSContext_t *ctx,
				       const uint8_t *requestData,
				       uint16_t requestLength)
{
	if (requestLength != UDS_SINGLE_VALUE_LEN) {
		OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
		UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
	} else {
		FO2_t fo2 = requestData[UDS_DATA_IDX];

		if (fo2 > FO2_MAX_PERCENT) {
			OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
			UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
		} else if (calibration_is_running()) {
			OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
			UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
		} else {
			/* Read current atmos pressure from zbus */
			uint16_t atmoPressure = 1013;
			(void)zbus_chan_read(&chan_atmos_pressure, &atmoPressure, K_NO_WAIT);

			CalRequest_t req = {
				.method = CAL_DIGITAL_REFERENCE,
				.fo2 = fo2,
				.pressure_mbar = atmoPressure,
			};
			(void)zbus_chan_pub(&chan_cal_request, &req, K_MSEC(100));

			ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
			ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
			ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
			ctx->responseLength = UDS_POS_RESP_HDR;
			UDS_SendResponse(ctx);
		}
	}

	return true;
}

static bool writeSettingSaveDID(UDSContext_t *ctx, uint16_t did,
				const uint8_t *requestData,
				uint16_t requestLength)
{
	uint8_t index = (uint8_t)(did - UDS_DID_SETTING_SAVE_BASE);

	/* Extract big-endian u64 value */
	uint64_t value = 0;
	uint16_t dataLen = requestLength - UDS_MIN_REQ_LEN;
	for (uint16_t i = 0; (i < dataLen) && (i < sizeof(uint64_t)); ++i) {
		value = (value << DIVECAN_BYTE_WIDTH) | requestData[UDS_DATA_IDX + i];
	}

	if (!UDS_SaveSettingValue(index, value)) {
		OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
		UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
	} else {
		ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
		ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
		ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
		ctx->responseLength = UDS_POS_RESP_HDR;
		UDS_SendResponse(ctx);
	}

	return true;
}

static bool writeSettingValueDID_handler(UDSContext_t *ctx, uint16_t did,
					 const uint8_t *requestData,
					 uint16_t requestLength)
{
	if (requestLength != SETTING_VALUE_WRITE_LEN) {
		OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
		UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
	} else {
		uint8_t index = (uint8_t)(did - UDS_DID_SETTING_VALUE_BASE);

		uint64_t value = 0;
		for (uint8_t i = 0; i < sizeof(uint64_t); ++i) {
			value = (value << DIVECAN_BYTE_WIDTH) | requestData[UDS_DATA_IDX + i];
		}

		if (!UDS_SetSettingValue(index, value)) {
			OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
			UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
		} else {
			ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
			ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
			ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
			ctx->responseLength = UDS_POS_RESP_HDR;
			UDS_SendResponse(ctx);
		}
	}

	return true;
}

/**
 * @brief Handle WriteDataByIdentifier (0x2E)
 */
static void HandleWriteDataByIdentifier(UDSContext_t *ctx,
					const uint8_t *requestData,
					uint16_t requestLength)
{
	if (requestLength < UDS_MIN_REQ_LEN) {
		OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
		UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
	} else {
		uint16_t did = (uint16_t)((uint16_t)requestData[UDS_DID_HI_IDX] << DIVECAN_BYTE_WIDTH) |
			       (uint16_t)requestData[UDS_DID_LO_IDX];

		if (did == UDS_DID_SETPOINT_WRITE) {
			(void)writeSetpointDID(ctx, requestData, requestLength);
		} else if (did == UDS_DID_CALIBRATION_TRIGGER) {
			(void)writeCalibrationTriggerDID(ctx, requestData, requestLength);
		} else if ((did >= UDS_DID_SETTING_SAVE_BASE) &&
			   (did < (UDS_DID_SETTING_SAVE_BASE + UDS_GetSettingCount()))) {
			(void)writeSettingSaveDID(ctx, did, requestData, requestLength);
		} else if ((did >= UDS_DID_SETTING_VALUE_BASE) &&
			   (did < (UDS_DID_SETTING_VALUE_BASE + UDS_GetSettingCount()))) {
			(void)writeSettingValueDID_handler(ctx, did, requestData, requestLength);
		} else {
			OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
			UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
		}
	}
}
