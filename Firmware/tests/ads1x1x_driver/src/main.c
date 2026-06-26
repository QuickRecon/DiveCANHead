/*
 * Driver-level regression test for the board-local multi-channel ADS1X1X driver
 * (drivers/ads1x1x). Two logical channels (different differential MUX pairs)
 * share ONE emulated ADS1115. The test proves each channel reads its own
 * MUX-specific value — i.e. the driver programs the correct MUX per channel.
 *
 * This is the layer that catches the original bug: the upstream driver rejects
 * channel_id != 0, so adc_channel_setup_dt(&ch1) below returns -ENOTSUP and the
 * test fails. native_sim integration tests use zephyr,adc-emul and never run the
 * real ti,ads1115 driver, so they are blind to this.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>

/* From the test devicetree (boards/native_sim.overlay): two channels on one
 * emulated ADS1115 — channel 0 = AIN0-AIN1, channel 1 = AIN2-AIN3. */
static const struct adc_dt_spec ch0 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct adc_dt_spec ch1 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

/* Emulator's deterministic value per MUX pair: MUX 0 -> 1000, MUX 3 -> 4000. */
#define EXPECT_CH0_AIN0_1 1000
#define EXPECT_CH1_AIN2_3 4000

static int read_channel(const struct adc_dt_spec *spec, int16_t *out)
{
	struct adc_sequence seq;
	int rc;

	(void)adc_sequence_init_dt(spec, &seq);
	seq.buffer = out;
	seq.buffer_size = sizeof(*out);

	rc = adc_read_dt(spec, &seq);
	return rc;
}

ZTEST(ads1x1x_multichannel, test_distinct_mux_per_channel)
{
	int16_t v0 = 0;
	int16_t v1 = 0;

	zassert_true(adc_is_ready_dt(&ch0), "ch0 ADC device not ready");
	zassert_true(adc_is_ready_dt(&ch1), "ch1 ADC device not ready");

	/* Both channels live on the SAME ADS1115. The upstream single-channel
	 * driver rejects channel_id 1 here with -ENOTSUP (the bug). */
	zassert_ok(adc_channel_setup_dt(&ch0), "cell-1 (AIN0-AIN1) channel setup failed");
	zassert_ok(adc_channel_setup_dt(&ch1), "cell-2 (AIN2-AIN3) channel setup failed");

	zassert_ok(read_channel(&ch0, &v0), "ch0 read failed");
	zassert_ok(read_channel(&ch1, &v1), "ch1 read failed");

	zassert_equal(v0, EXPECT_CH0_AIN0_1,
		      "ch0 (AIN0-AIN1) expected %d, got %d", EXPECT_CH0_AIN0_1, v0);
	zassert_equal(v1, EXPECT_CH1_AIN2_3,
		      "ch1 (AIN2-AIN3) expected %d, got %d", EXPECT_CH1_AIN2_3, v1);
	zassert_not_equal(v0, v1,
			  "both channels read the same MUX — not actually multiplexed");
}

/* Re-reading must keep selecting the right MUX (the fix programs the full CONFIG
 * word per conversion, so an interleaved read of the other channel can't leave a
 * stale MUX behind). */
ZTEST(ads1x1x_multichannel, test_interleaved_reads_stay_independent)
{
	int16_t a = 0;
	int16_t b = 0;
	int16_t c = 0;

	zassert_ok(adc_channel_setup_dt(&ch0));
	zassert_ok(adc_channel_setup_dt(&ch1));

	zassert_ok(read_channel(&ch0, &a));
	zassert_ok(read_channel(&ch1, &b));
	zassert_ok(read_channel(&ch0, &c)); /* ch0 again after ch1 */

	zassert_equal(a, EXPECT_CH0_AIN0_1, "first ch0 read %d", a);
	zassert_equal(b, EXPECT_CH1_AIN2_3, "ch1 read %d", b);
	zassert_equal(c, EXPECT_CH0_AIN0_1, "ch0 re-read %d (stale MUX?)", c);
}

ZTEST_SUITE(ads1x1x_multichannel, NULL, NULL, NULL, NULL, NULL);
