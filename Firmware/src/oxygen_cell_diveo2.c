/* strtok_r requires POSIX source on native_sim (host libc) */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "power_management.h"
#include "errors.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

LOG_MODULE_REGISTER(cell_diveo2, LOG_LEVEL_INF);

/* Newline for terminating uart message */
#define DIVEO2_NEWLINE        0x0DU
#define DIVEO2_RX_BUFFER_LEN  86U
#define DIVEO2_TX_BUFFER_LEN  8U

/* Digital cell error codes */
#define WARN_NEAR_SAT       0x1U
#define ERR_LOW_INTENSITY    0x2U
#define ERR_HIGH_SIGNAL      0x4U
#define ERR_LOW_SIGNAL       0x8U
#define ERR_HIGH_REF         0x10U
#define ERR_TEMP             0x20U
#define WARN_HUMIDITY_HIGH   0x40U
#define WARN_PRESSURE        0x80U
#define WARN_HUMIDITY_FAIL   0x100U

/* Cell commands */
#define GET_DETAIL_COMMAND   "#DRAW"
#define GET_OXY_COMMAND      "#DOXY"
#define STRTOL_BASE          10

/* Timeouts */
#define DIGITAL_RESPONSE_TIMEOUT_MS 1000
#define CELL_STARTUP_DELAY_MS       1000
#define MIN_SAMPLE_INTERVAL_MS      100
#define UART_RX_TIMEOUT_MS          2000

/* ---- Parse functions (pure, no OS deps — testable) ---- */

CellStatus_t diveo2_parse_error_code(const char *err_str)
{
	CellStatus_t status = CELL_OK;

	if (err_str != NULL) {
		uint32_t errCode = (uint16_t)(strtol(err_str, NULL,
						     STRTOL_BASE));
		/* Check for error states */
		if ((errCode &
		     (ERR_LOW_INTENSITY | ERR_HIGH_SIGNAL |
		      ERR_LOW_SIGNAL | ERR_HIGH_REF | ERR_TEMP)) != 0U) {
			OP_ERROR_DETAIL(OP_ERR_CELL_FAILURE, errCode);
			status = CELL_FAIL;
		} else if ((errCode &
			    (WARN_HUMIDITY_FAIL | WARN_PRESSURE |
			     WARN_HUMIDITY_HIGH | WARN_NEAR_SAT)) != 0U) {
			OP_ERROR_DETAIL(OP_ERR_CELL_FAILURE, errCode);
			status = CELL_DEGRADED;
		} else if (errCode > 0U) {
			/* Unknown error */
			OP_ERROR_DETAIL(OP_ERR_UNKNOWN, errCode);
			status = CELL_FAIL;
		} else {
			/* No action — status already CELL_OK */
		}
	} else {
		status = CELL_FAIL;
	}

	return status;
}

size_t diveo2_prepare_message_buffer(const char *rawBuffer, char *outBuffer,
				     size_t outBufferLen)
{
	size_t skipped = 0;

	if ((rawBuffer != NULL) && (outBuffer != NULL) && (outBufferLen > 0U)) {
		const char *msgBuf = rawBuffer;

		/* Skip leading junk (nulls and newlines) */
		while (((0 == msgBuf[0]) ||
			(DIVEO2_NEWLINE == (uint8_t)msgBuf[0])) &&
		       (skipped < (outBufferLen - 1U))) {
			++msgBuf;
			++skipped;
		}

		/* Copy to output buffer */
		size_t copyLen = outBufferLen - skipped;

		if (copyLen > outBufferLen) {
			copyLen = outBufferLen;
		}
		(void)strncpy(outBuffer, msgBuf, copyLen);
		outBuffer[outBufferLen - 1U] = '\0';

		/* Null-terminate at first newline */
		outBuffer[strcspn(outBuffer, "\r\n")] = '\0';
	} else {
		if (outBuffer != NULL) {
			outBuffer[0] = '\0';
		}
	}

	return skipped;
}

bool diveo2_parse_simple_response(const char *message, int32_t *ppo2,
				  int32_t *temperature, CellStatus_t *status)
{
	bool success = false;

	if ((message != NULL) && (ppo2 != NULL) &&
	    (temperature != NULL) && (status != NULL)) {
		char msgCopy[DIVEO2_RX_BUFFER_LEN];

		(void)strncpy(msgCopy, message, sizeof(msgCopy) - 1U);
		msgCopy[sizeof(msgCopy) - 1U] = '\0';

		const char *const sep = " ";
		char *saveptr = NULL;
		const char *cmdName = strtok_r(msgCopy, sep, &saveptr);

		if ((cmdName != NULL) &&
		    (0 == strcmp(cmdName, GET_OXY_COMMAND))) {
			const char *ppo2Str = strtok_r(NULL, sep, &saveptr);
			const char *tempStr = strtok_r(NULL, sep, &saveptr);
			const char *errStr = strtok_r(NULL, sep, &saveptr);

			if ((ppo2Str != NULL) && (tempStr != NULL) &&
			    (errStr != NULL)) {
				*ppo2 = strtol(ppo2Str, NULL, STRTOL_BASE);
				*temperature = strtol(tempStr, NULL,
						      STRTOL_BASE);
				*status = diveo2_parse_error_code(errStr);
				success = true;
			} else {
				/* Missing fields */
				OP_ERROR(OP_ERR_CELL_FAILURE);
			}
		} else {
			/* Wrong command or null */
			OP_ERROR(OP_ERR_CELL_FAILURE);
		}
	} else {
		/* Null arguments */
		OP_ERROR(OP_ERR_NULL_PTR);
	}

	return success;
}

bool diveo2_parse_detailed_response(const char *message, int32_t *ppo2,
				    int32_t *temperature, int32_t *errCode,
				    int32_t *phase, int32_t *intensity,
				    int32_t *ambientLight, int32_t *pressure,
				    int32_t *humidity, CellStatus_t *status)
{
	bool success = false;

	if ((message != NULL) && (ppo2 != NULL) && (temperature != NULL) &&
	    (errCode != NULL) && (phase != NULL) && (intensity != NULL) &&
	    (ambientLight != NULL) && (pressure != NULL) &&
	    (humidity != NULL) && (status != NULL)) {
		char msgCopy[DIVEO2_RX_BUFFER_LEN];

		(void)strncpy(msgCopy, message, sizeof(msgCopy) - 1U);
		msgCopy[sizeof(msgCopy) - 1U] = '\0';

		const char *const sep = " ";
		char *saveptr = NULL;
		const char *cmdName = strtok_r(msgCopy, sep, &saveptr);

		if ((cmdName != NULL) &&
		    (0 == strcmp(cmdName, GET_DETAIL_COMMAND))) {
			const char *ppo2Str = strtok_r(NULL, sep, &saveptr);
			const char *tempStr = strtok_r(NULL, sep, &saveptr);
			const char *errStr = strtok_r(NULL, sep, &saveptr);
			const char *phaseStr = strtok_r(NULL, sep, &saveptr);
			const char *intensityStr = strtok_r(NULL, sep,
							    &saveptr);
			const char *ambientStr = strtok_r(NULL, sep, &saveptr);
			const char *pressureStr = strtok_r(NULL, sep,
							   &saveptr);
			const char *humidityStr = strtok_r(NULL, sep,
							   &saveptr);

			if ((ppo2Str != NULL) && (tempStr != NULL) &&
			    (errStr != NULL) && (phaseStr != NULL) &&
			    (intensityStr != NULL) && (ambientStr != NULL) &&
			    (pressureStr != NULL) && (humidityStr != NULL)) {
				*ppo2 = strtol(ppo2Str, NULL, STRTOL_BASE);
				*temperature = strtol(tempStr, NULL,
						      STRTOL_BASE);
				*errCode = strtol(errStr, NULL, STRTOL_BASE);
				*phase = strtol(phaseStr, NULL, STRTOL_BASE);
				*intensity = strtol(intensityStr, NULL,
						    STRTOL_BASE);
				*ambientLight = strtol(ambientStr, NULL,
						       STRTOL_BASE);
				*pressure = strtol(pressureStr, NULL,
						   STRTOL_BASE);
				*humidity = strtol(humidityStr, NULL,
						   STRTOL_BASE);
				*status = diveo2_parse_error_code(errStr);
				success = true;
			} else {
				/* Missing fields */
				OP_ERROR(OP_ERR_CELL_FAILURE);
			}
		} else {
			/* Wrong command or null */
			OP_ERROR(OP_ERR_CELL_FAILURE);
		}
	} else {
		/* Null arguments */
		OP_ERROR(OP_ERR_NULL_PTR);
	}

	return success;
}

void diveo2_format_tx_command(const char *command, uint8_t *txBuf,
			      size_t bufLen)
{
	if ((command != NULL) && (txBuf != NULL) && (bufLen > 0U)) {
		(void)memset(txBuf, 0, bufLen);
		/* Copy the string into the all-zero buffer, then replace
		 * the first zero with a newline (CR) */
		(void)strncpy((char *)txBuf, command, bufLen - 1U);
		txBuf[strcspn((char *)txBuf, "\0")] = DIVEO2_NEWLINE;
	}
}

/* ---- Thread state and implementation ---- */

struct diveo2_cell_state {
	uint8_t cell_number;
	const struct device *uart_dev;
	CalCoeff_t cal_coeff;
	CellStatus_t status;
	int32_t cell_sample;
	int32_t temperature;
	int32_t pressure;
	int32_t humidity;
	int64_t last_ppo2_ticks;
	char last_message[DIVEO2_RX_BUFFER_LEN];
	uint8_t rx_buf[DIVEO2_RX_BUFFER_LEN];
	uint8_t tx_buf[DIVEO2_TX_BUFFER_LEN];
	struct k_sem rx_sem;
	size_t rx_len;
	const struct zbus_channel *out_chan;
};

/* UART async callback — signals the cell thread when RX completes or idles */
static void diveo2_uart_callback(const struct device *dev,
				 struct uart_event *evt, void *user_data)
{
	struct diveo2_cell_state *cell = user_data;

	switch (evt->type) {
	case UART_RX_RDY:
		/* Data received into the RX buffer — copy to last_message */
		if (evt->data.rx.len < DIVEO2_RX_BUFFER_LEN) {
			(void)memcpy(cell->last_message,
				     &evt->data.rx.buf[evt->data.rx.offset],
				     evt->data.rx.len);
			cell->last_message[evt->data.rx.len] = '\0';
			cell->rx_len = evt->data.rx.len;
		}
		break;
	case UART_RX_DISABLED:
		/* RX finished (idle timeout or buffer full) — wake the thread */
		k_sem_give(&cell->rx_sem);
		break;
	default:
		break;
	}
}

static void diveo2_send_command(struct diveo2_cell_state *cell,
				const char *command)
{
	diveo2_format_tx_command(command, cell->tx_buf, sizeof(cell->tx_buf));
	size_t len = strlen((char *)cell->tx_buf) + 1U;

	(void)uart_tx(cell->uart_dev, cell->tx_buf, len, SYS_FOREVER_US);
}

static void diveo2_broadcast(struct diveo2_cell_state *cell)
{
	PPO2_t ppo2 = 0;
	int64_t now = k_uptime_ticks();
	int64_t timeout_ticks = k_ms_to_ticks_ceil64(
		DIGITAL_RESPONSE_TIMEOUT_MS);

	/* First we check our timeouts to make sure we're not giving stale info */
	if ((now - cell->last_ppo2_ticks) > timeout_ticks) {
		/* If we've taken longer than timeout, fail the cell, no lies here */
		cell->status = CELL_FAIL;
		OP_ERROR(OP_ERR_OUT_OF_DATE);
	}

	/* Check our vbus voltage to ensure we're above 3.25V.
	 * Digital cells need a stable power supply — below this voltage
	 * the cell readings are unreliable. On Jr, VBUS == battery voltage. */
	static const float VBUS_MIN_VOLTAGE = 3.25f;
	float vbus_v = power_get_vbus_voltage(POWER_DEVICE);

	if ((vbus_v > 0.0f) && (vbus_v < VBUS_MIN_VOLTAGE)) {
		cell->status = CELL_FAIL;
		OP_ERROR_DETAIL(OP_ERR_VBUS_UNDERVOLT,
				(uint32_t)(vbus_v * 1000.0f));
	}

	/* Our coefficient is simply the float needed to make the current sample
	 * the current PPO2. Yes this is backwards compared to the analog cell,
	 * but it makes more intuitive sense when looking at the values to see
	 * how deviated the cell is from OEM spec */
	double precision_ppo2 = (double)cell->cell_sample /
				(double)cell->cal_coeff;
	double temp_ppo2 = precision_ppo2 * 100.0;

	if (temp_ppo2 > 255.0) {
		cell->status = CELL_FAIL;
		OP_ERROR_DETAIL(OP_ERR_CELL_OVERRANGE, (uint32_t)temp_ppo2);
	}
	ppo2 = (PPO2_t)(temp_ppo2);

	OxygenCellMsg_t msg = {
		.cell_number = cell->cell_number,
		.ppo2 = ppo2,
		.precision_ppo2 = precision_ppo2,
		.millivolts = 0,
		.status = cell->status,
		.timestamp_ticks = k_uptime_ticks(),
		.pressure_uhpa = (uint32_t)cell->pressure,
	};

	(void)zbus_chan_pub(cell->out_chan, &msg, K_MSEC(100));
}

static void diveo2_load_cal(struct diveo2_cell_state *cell)
{
	char key[16];

	(void)snprintf(key, sizeof(key), "cal/cell%u", cell->cell_number);

	CalCoeff_t coeff = 0.0f;
	int len = settings_runtime_get(key, &coeff, sizeof(coeff));

	if ((len == sizeof(coeff)) &&
	    (coeff > DIVEO2_CAL_LOWER) && (coeff < DIVEO2_CAL_UPPER)) {
		cell->cal_coeff = coeff;
		cell->status = CELL_OK;
		LOG_INF("DiveO2 cell %u: loaded cal coeff %.0f",
			cell->cell_number, (double)coeff);
	} else {
		/* Bug #3 fix: set CELL_NEED_CAL when cal is missing or out of
		 * range (old code incorrectly defaulted to CELL_OK) */
		cell->cal_coeff = DIVEO2_CAL_DEFAULT;
		cell->status = CELL_NEED_CAL;
		LOG_WRN("DiveO2 cell %u: no valid cal, defaulting",
			cell->cell_number);
	}
}

static void diveo2_cell_thread(void *p1, void *p2, void *p3)
{
	struct diveo2_cell_state *cell = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_sem_init(&cell->rx_sem, 0, 1);

	if (!device_is_ready(cell->uart_dev)) {
		LOG_ERR("UART not ready for cell %u", cell->cell_number);
		return;
	}

	int ret = uart_callback_set(cell->uart_dev, diveo2_uart_callback, cell);

	if (ret != 0) {
		LOG_ERR("Failed to set UART callback for cell %u: %d",
			cell->cell_number, ret);
		return;
	}

	/* Load calibration coefficient from NVS */
	diveo2_load_cal(cell);

	/* The cell needs 1 second to power up before its ready to deal with
	 * commands. So we lodge a failure datapoint while we spool up — we're
	 * failed while we start, we have no valid PPO2 data to give */
	cell->status = CELL_FAIL;

	OxygenCellMsg_t init_msg = {
		.cell_number = cell->cell_number,
		.ppo2 = 0,
		.precision_ppo2 = 0.0,
		.millivolts = 0,
		.status = cell->status,
		.timestamp_ticks = k_uptime_ticks(),
	};
	(void)zbus_chan_pub(cell->out_chan, &init_msg, K_MSEC(100));

	/* Do the wait for cell startup */
	k_msleep(CELL_STARTUP_DELAY_MS);

	while (true) {
		int64_t loop_start = k_uptime_ticks();

		/* Send #DRAW command and start RX with idle timeout */
		(void)memset(cell->rx_buf, 0, sizeof(cell->rx_buf));
		cell->rx_len = 0;
		(void)uart_rx_enable(cell->uart_dev, cell->rx_buf,
				     sizeof(cell->rx_buf),
				     UART_RX_TIMEOUT_MS * 1000);

		diveo2_send_command(cell, GET_DETAIL_COMMAND);

		/* Wait for RX complete (idle line detection) or timeout */
		if (k_sem_take(&cell->rx_sem,
			       K_MSEC(UART_RX_TIMEOUT_MS)) == 0) {
			char msgArray[DIVEO2_RX_BUFFER_LEN] = {0};

			(void)diveo2_prepare_message_buffer(
				cell->last_message, msgArray, sizeof(msgArray));

			int32_t ppo2 = 0;
			int32_t temp = 0;
			int32_t errCode = 0;
			int32_t phase = 0;
			int32_t intensity = 0;
			int32_t ambient = 0;
			int32_t pressure = 0;
			int32_t humidity = 0;
			CellStatus_t rx_status = CELL_FAIL;

			/* Try detailed response first, then simple */
			if (diveo2_parse_detailed_response(
				    msgArray, &ppo2, &temp, &errCode,
				    &phase, &intensity, &ambient,
				    &pressure, &humidity, &rx_status)) {
				cell->cell_sample = ppo2;
				cell->temperature = temp;
				cell->pressure = pressure;
				cell->humidity = humidity;
				cell->status = rx_status;
				cell->last_ppo2_ticks = k_uptime_ticks();
			} else if (diveo2_parse_simple_response(
					   msgArray, &ppo2, &temp,
					   &rx_status)) {
				cell->cell_sample = ppo2;
				cell->temperature = temp;
				cell->status = rx_status;
				cell->last_ppo2_ticks = k_uptime_ticks();
			} else {
				LOG_WRN("Cell %u: unknown message: %s",
					cell->cell_number, msgArray);
				k_msleep(500);
			}
		} else {
			OP_ERROR(OP_ERR_TIMEOUT);
			(void)uart_rx_disable(cell->uart_dev);
		}

		diveo2_broadcast(cell);

		/* Sampling more than 10x per second is a bit excessive,
		 * if the cell is getting back to us that quick we can take a break */
		int64_t elapsed_ms = k_ticks_to_ms_ceil64(
			k_uptime_ticks() - loop_start);

		if (elapsed_ms < MIN_SAMPLE_INTERVAL_MS) {
			k_msleep(MIN_SAMPLE_INTERVAL_MS - (int32_t)elapsed_ms);
		}
	}
}

/* ---- Per-cell static state and threads ---- */

/* Cell → UART mapping: USART1 = Cell 1, USART2 = Cell 2, USART3 = Cell 3 */

#if defined(CONFIG_CELL_1_TYPE_DIVEO2)
static struct diveo2_cell_state diveo2_cell_1 = {
	.cell_number = 0,
	.cal_coeff = DIVEO2_CAL_DEFAULT,
	.status = CELL_NEED_CAL,
	.out_chan = &chan_cell_1,
	.uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1)),
};
K_THREAD_DEFINE(diveo2_thread_1, 1024,
		diveo2_cell_thread, &diveo2_cell_1, NULL, NULL,
		7, 0, 0);
#endif

#if defined(CONFIG_CELL_2_TYPE_DIVEO2) && CONFIG_CELL_COUNT >= 2
static struct diveo2_cell_state diveo2_cell_2 = {
	.cell_number = 1,
	.cal_coeff = DIVEO2_CAL_DEFAULT,
	.status = CELL_NEED_CAL,
	.out_chan = &chan_cell_2,
	.uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart2)),
};
K_THREAD_DEFINE(diveo2_thread_2, 1024,
		diveo2_cell_thread, &diveo2_cell_2, NULL, NULL,
		7, 0, 0);
#endif

#if defined(CONFIG_CELL_3_TYPE_DIVEO2) && CONFIG_CELL_COUNT >= 3
static struct diveo2_cell_state diveo2_cell_3 = {
	.cell_number = 2,
	.cal_coeff = DIVEO2_CAL_DEFAULT,
	.status = CELL_NEED_CAL,
	.out_chan = &chan_cell_3,
	.uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3)),
};
K_THREAD_DEFINE(diveo2_thread_3, 1024,
		diveo2_cell_thread, &diveo2_cell_3, NULL, NULL,
		7, 0, 0);
#endif
