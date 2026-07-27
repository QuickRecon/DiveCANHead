/*
 * Minimal ADS1115 I2C register emulator for the multi-channel driver ztest.
 *
 * Models the CONV (0x00) and CONFIG (0x01) registers. On a CONFIG write with
 * the OS start bit set it latches the selected MUX and produces a CONV value
 * that is a deterministic function of the MUX pair. The test reads two logical
 * channels (different MUX pairs) on ONE emulated ADS1115 and asserts each
 * returns its pair-specific value — proving the driver programs the right MUX
 * per channel. With the upstream single-channel driver the second channel never
 * sets up, so the test fails there: it is a regression guard for the fix.
 */

#define DT_DRV_COMPAT ti_ads1115

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ads1115_emul, LOG_LEVEL_INF);

#define ADS1115_REG_CONV   0x00
#define ADS1115_REG_CONFIG 0x01
#define ADS1115_NUM_REGS   4
#define ADS1115_CONFIG_OS  BIT(15)

struct ads1115_emul_data {
	uint16_t reg[ADS1115_NUM_REGS]; /* CONV, CONFIG, LO_THRESH, HI_THRESH */
};

/* Distinct, deterministic conversion value per differential MUX selection so
 * the test can tell which pair the driver actually programmed:
 *   MUX 0 = AIN0-AIN1 -> 1000,  MUX 3 = AIN2-AIN3 -> 4000, etc. */
static int16_t ads1115_emul_conv_for_mux(uint8_t mux)
{
	return (int16_t)(1000 * (mux + 1));
}

static int ads1115_emul_transfer(const struct emul *target, struct i2c_msg *msgs,
				 int num_msgs, int addr)
{
	struct ads1115_emul_data *data = target->data;

	ARG_UNUSED(addr);
	__ASSERT_NO_MSG((NULL != msgs) && (0 != num_msgs));

	/* Register write: [reg, hi, lo] (ads1x1x_write_reg). */
	if (num_msgs == 1 && (msgs[0].flags & I2C_MSG_READ) == 0) {
		uint8_t reg;
		uint16_t val;

		if (msgs[0].len != 3) {
			LOG_ERR("unexpected write len %d", msgs[0].len);
			return -EIO;
		}
		reg = msgs[0].buf[0];
		val = sys_get_be16(&msgs[0].buf[1]);
		if (reg >= ADS1115_NUM_REGS) {
			return -EIO;
		}
		data->reg[reg] = val;

		if (reg == ADS1115_REG_CONFIG && (val & ADS1115_CONFIG_OS)) {
			/* Start conversion: latch MUX (bits [14:12]) and produce
			 * its CONV value. Keep OS set so the driver's
			 * wait_data_ready() sees "conversion ready" immediately. */
			uint8_t mux = (val >> 12) & 0x7;

			data->reg[ADS1115_REG_CONV] =
				(uint16_t)ads1115_emul_conv_for_mux(mux);
		}
		return 0;
	}

	/* Register read: write reg pointer (1B), read 2B (i2c_burst_read_dt). */
	if (num_msgs == 2 && (msgs[0].flags & I2C_MSG_READ) == 0 &&
	    (msgs[1].flags & I2C_MSG_READ) != 0) {
		uint8_t reg;

		if (msgs[0].len != 1 || msgs[1].len != 2) {
			LOG_ERR("unexpected read framing %d/%d", msgs[0].len, msgs[1].len);
			return -EIO;
		}
		reg = msgs[0].buf[0];
		if (reg >= ADS1115_NUM_REGS) {
			return -EIO;
		}
		sys_put_be16(data->reg[reg], msgs[1].buf);
		return 0;
	}

	LOG_ERR("unsupported transfer: num_msgs=%d", num_msgs);
	return -EIO;
}

static int ads1115_emul_init(const struct emul *target, const struct device *parent)
{
	struct ads1115_emul_data *data = target->data;

	ARG_UNUSED(parent);

	/* Power-on reset values; OS=1 means "not currently converting". */
	data->reg[ADS1115_REG_CONV] = 0;
	data->reg[ADS1115_REG_CONFIG] = ADS1115_CONFIG_OS;
	data->reg[2] = 0x8000; /* LO_THRESH reset */
	data->reg[3] = 0x7FFF; /* HI_THRESH reset */
	return 0;
}

static const struct i2c_emul_api ads1115_emul_api_i2c = {
	.transfer = ads1115_emul_transfer,
};

#define ADS1115_EMUL(n)								\
	static struct ads1115_emul_data ads1115_emul_data_##n;			\
	EMUL_DT_INST_DEFINE(n, ads1115_emul_init, &ads1115_emul_data_##n,	\
			    NULL, &ads1115_emul_api_i2c, NULL);

DT_INST_FOREACH_STATUS_OKAY(ADS1115_EMUL)
