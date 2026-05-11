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
#include "errors.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

LOG_MODULE_REGISTER(cell_o2s, LOG_LEVEL_INF);

/* Newline for terminating uart message */
#define O2S_NEWLINE           0x0AU
#define O2S_RX_BUFFER_LEN    10U
#define O2S_TX_BUFFER_LEN    4U

/* Cell commands */
#define GET_OXY_COMMAND      "Mm"
#define GET_OXY_RESPONSE     "Mn"
#define STRTOF_SEP           ":"

/* Timeouts */
#define DIGITAL_RESPONSE_TIMEOUT_MS 2000
#define CELL_STARTUP_DELAY_MS       1000
#define SAMPLE_INTERVAL_MS          500
#define UART_RX_TIMEOUT_MS          1000

/* ---- Parse functions (pure, no OS deps — testable) ---- */

size_t o2s_prepare_message_buffer(const char *rawBuffer, char *outBuffer,
				  size_t outBufferLen)
{
	size_t skipped = 0U;

	if ((rawBuffer != NULL) && (outBuffer != NULL) && (outBufferLen > 0U)) {
		/* Zero the output buffer first to ensure null termination */
		(void)memset(outBuffer, 0, outBufferLen);

		const char *msgBuf = rawBuffer;

		/* Skip leading junk (nulls and newlines) */
		while (((0 == msgBuf[0]) || (O2S_NEWLINE == msgBuf[0])) &&
		       (skipped < (outBufferLen - 1U))) {
			++msgBuf;
			++skipped;
		}

		size_t copyLen = outBufferLen - 1U;

		(void)strncpy(outBuffer, msgBuf, copyLen);
		outBuffer[outBufferLen - 1U] = '\0';
		/* Strip trailing CR/LF */
		outBuffer[strcspn(outBuffer, "\r\n")] = '\0';
	} else {
		if (outBuffer != NULL) {
			outBuffer[0] = '\0';
		}
	}

	return skipped;
}

bool o2s_parse_response(const char *message, float *ppo2)
{
	bool success = false;

	if ((message != NULL) && (ppo2 != NULL)) {
		char msgCopy[O2S_RX_BUFFER_LEN];

		(void)strncpy(msgCopy, message, sizeof(msgCopy) - 1U);
		msgCopy[sizeof(msgCopy) - 1U] = '\0';

		const char *const sep = STRTOF_SEP;
		char *saveptr = NULL;
		const char *cmdName = strtok_r(msgCopy, sep, &saveptr);

		if ((cmdName != NULL) &&
		    ((0 == strcmp(cmdName, GET_OXY_RESPONSE)) ||
		     (0 == strcmp(cmdName, GET_OXY_COMMAND)))) {
			const char *ppo2Str = strtok_r(NULL, sep, &saveptr);

			if (ppo2Str != NULL) {
				*ppo2 = strtof(ppo2Str, NULL);
				success = true;
			}
			/* NULL ppo2Str means we got echo only — return false */
		}
	}

	return success;
}

void o2s_format_tx_command(const char *command, uint8_t *txBuf, size_t bufLen)
{
	if ((command != NULL) && (txBuf != NULL) && (bufLen > 0U)) {
		(void)memset(txBuf, 0, bufLen);
		(void)strncpy((char *)txBuf, command, bufLen - 1U);
		txBuf[strcspn((char *)txBuf, "\0")] = O2S_NEWLINE;
	}
}

/* ---- Thread state and implementation ---- */

struct o2s_cell_state {
	uint8_t cell_number;
	const struct device *uart_dev;
	CalCoeff_t cal_coeff;
	CellStatus_t status;
	float cell_sample;
	int64_t last_ppo2_ticks;
	char last_message[O2S_RX_BUFFER_LEN];
	uint8_t rx_buf[O2S_RX_BUFFER_LEN];
	uint8_t tx_buf[O2S_TX_BUFFER_LEN];
	struct k_sem rx_sem;
	size_t rx_len;
	const struct zbus_channel *out_chan;
};

/* UART async callback — signals the cell thread when RX completes or idles */
static void o2s_uart_callback(const struct device *dev,
			      struct uart_event *evt, void *user_data)
{
	struct o2s_cell_state *cell = user_data;

	switch (evt->type) {
	case UART_RX_RDY:
		if (evt->data.rx.len < O2S_RX_BUFFER_LEN) {
			(void)memcpy(cell->last_message,
				     &evt->data.rx.buf[evt->data.rx.offset],
				     evt->data.rx.len);
			cell->last_message[evt->data.rx.len] = '\0';
			cell->rx_len = evt->data.rx.len;
		}
		break;
	case UART_RX_DISABLED:
		k_sem_give(&cell->rx_sem);
		break;
	default:
		break;
	}
}

static void o2s_send_command(struct o2s_cell_state *cell, const char *command)
{
	o2s_format_tx_command(command, cell->tx_buf, sizeof(cell->tx_buf));
	size_t len = strlen((char *)cell->tx_buf) + 1U;

	(void)uart_tx(cell->uart_dev, cell->tx_buf, len, SYS_FOREVER_US);
}

static void o2s_broadcast(struct o2s_cell_state *cell)
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

	/* Our coefficient is simply the float needed to make the current sample
	 * the current PPO2. Yes this is backwards compared to the analog cell,
	 * but it makes more intuitive sense when looking at the values to see
	 * how deviated the cell is from OEM spec */
	float temp_ppo2 = cell->cell_sample * cell->cal_coeff * 100.0f;

	if (temp_ppo2 > 255.0f) {
		cell->status = CELL_FAIL;
		OP_ERROR_DETAIL(OP_ERR_CELL_OVERRANGE, (uint32_t)temp_ppo2);
	}
	ppo2 = (PPO2_t)(temp_ppo2);

	double precision_ppo2 = (double)cell->cell_sample *
				(double)cell->cal_coeff;

	OxygenCellMsg_t msg = {
		.cell_number = cell->cell_number,
		.ppo2 = ppo2,
		.precision_ppo2 = precision_ppo2,
		.millivolts = 0,
		.status = cell->status,
		.timestamp_ticks = k_uptime_ticks(),
	};

	(void)zbus_chan_pub(cell->out_chan, &msg, K_MSEC(100));
}

static void o2s_load_cal(struct o2s_cell_state *cell)
{
	char key[16];

	(void)snprintf(key, sizeof(key), "cal/cell%u", cell->cell_number);

	CalCoeff_t coeff = 0.0f;
	int len = settings_runtime_get(key, &coeff, sizeof(coeff));

	if ((len == sizeof(coeff)) &&
	    (coeff > O2S_CAL_LOWER) && (coeff < O2S_CAL_UPPER)) {
		cell->cal_coeff = coeff;
		cell->status = CELL_OK;
		LOG_INF("O2S cell %u: loaded cal coeff %d.%03d",
			cell->cell_number,
			(int)coeff, (int)((coeff - (int)coeff) * 1000));
	} else {
		/* Bug #3 fix: set CELL_NEED_CAL when cal is missing or out of
		 * range (old code incorrectly defaulted to CELL_OK) */
		cell->cal_coeff = O2S_CAL_DEFAULT;
		cell->status = CELL_NEED_CAL;
		LOG_WRN("O2S cell %u: no valid cal, defaulting",
			cell->cell_number);
	}
}

static void o2s_cell_thread(void *p1, void *p2, void *p3)
{
	struct o2s_cell_state *cell = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_sem_init(&cell->rx_sem, 0, 1);

	if (!device_is_ready(cell->uart_dev)) {
		LOG_ERR("UART not ready for O2S cell %u", cell->cell_number);
		return;
	}

	int ret = uart_callback_set(cell->uart_dev, o2s_uart_callback, cell);

	if (ret != 0) {
		LOG_ERR("Failed to set UART callback for O2S cell %u: %d",
			cell->cell_number, ret);
		return;
	}

	/* Load calibration coefficient from NVS */
	o2s_load_cal(cell);

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
		/* Start RX before sending command (half-duplex: cell echoes
		 * the command then sends response) */
		(void)memset(cell->rx_buf, 0, sizeof(cell->rx_buf));
		cell->rx_len = 0;
		(void)uart_rx_enable(cell->uart_dev, cell->rx_buf,
				     sizeof(cell->rx_buf),
				     UART_RX_TIMEOUT_MS * 1000);

		o2s_send_command(cell, GET_OXY_COMMAND);

		if (k_sem_take(&cell->rx_sem,
			       K_MSEC(UART_RX_TIMEOUT_MS)) == 0) {
			char msgArray[O2S_RX_BUFFER_LEN] = {0};

			(void)o2s_prepare_message_buffer(
				cell->last_message, msgArray, sizeof(msgArray));

			float ppo2 = 0.0f;

			if (o2s_parse_response(msgArray, &ppo2)) {
				cell->cell_sample = ppo2;
				cell->status = CELL_OK;
				cell->last_ppo2_ticks = k_uptime_ticks();
				o2s_broadcast(cell);
			} else {
				LOG_WRN("O2S cell %u: unknown: %s",
					cell->cell_number, msgArray);
			}
		} else {
			OP_ERROR(OP_ERR_TIMEOUT);
			(void)uart_rx_disable(cell->uart_dev);
		}

		/* O2S samples at ~1Hz, wait between samples */
		k_msleep(SAMPLE_INTERVAL_MS);
	}
}

/* ---- Per-cell static state and threads ---- */

/* Cell → UART mapping: USART1 = Cell 1, USART2 = Cell 2, USART3 = Cell 3 */

#if defined(CONFIG_CELL_1_TYPE_O2S)
static struct o2s_cell_state o2s_cell_1 = {
	.cell_number = 0,
	.cal_coeff = O2S_CAL_DEFAULT,
	.status = CELL_NEED_CAL,
	.out_chan = &chan_cell_1,
	.uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1)),
};
K_THREAD_DEFINE(o2s_thread_1, 768,
		o2s_cell_thread, &o2s_cell_1, NULL, NULL,
		7, 0, 0);
#endif

#if defined(CONFIG_CELL_2_TYPE_O2S) && CONFIG_CELL_COUNT >= 2
static struct o2s_cell_state o2s_cell_2 = {
	.cell_number = 1,
	.cal_coeff = O2S_CAL_DEFAULT,
	.status = CELL_NEED_CAL,
	.out_chan = &chan_cell_2,
	.uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart2)),
};
K_THREAD_DEFINE(o2s_thread_2, 768,
		o2s_cell_thread, &o2s_cell_2, NULL, NULL,
		7, 0, 0);
#endif

#if defined(CONFIG_CELL_3_TYPE_O2S) && CONFIG_CELL_COUNT >= 3
static struct o2s_cell_state o2s_cell_3 = {
	.cell_number = 2,
	.cal_coeff = O2S_CAL_DEFAULT,
	.status = CELL_NEED_CAL,
	.out_chan = &chan_cell_3,
	.uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3)),
};
K_THREAD_DEFINE(o2s_thread_3, 768,
		o2s_cell_thread, &o2s_cell_3, NULL, NULL,
		7, 0, 0);
#endif
