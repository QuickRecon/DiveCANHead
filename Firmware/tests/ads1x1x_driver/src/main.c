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
#include <zephyr/dt-bindings/adc/adc.h>

/* From the test devicetree (boards/native_sim.overlay): two channels on one
 * emulated ADS1115 — channel 0 = AIN0-AIN1, channel 1 = AIN2-AIN3. */
static const struct adc_dt_spec ch0 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct adc_dt_spec ch1 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

/* Emulator's deterministic value per MUX pair: MUX 0 -> 1000, MUX 3 -> 4000. */
#define EXPECT_CH0_AIN0_1 1000
#define EXPECT_CH1_AIN2_3 4000

/* Fault-injection hooks implemented in ads1115_emul.c — let the tests drive the
 * driver's I2C-error and conversion-not-ready branches. */
void ads1115_emul_fault_reset(void);
void ads1115_emul_fail_writes(bool on);
void ads1115_emul_fail_read_at(int nth);
void ads1115_emul_os_busy_reads(int n);

static int read_channel(const struct adc_dt_spec *spec, int16_t *out)
{
	/* Zero-initialise: adc_sequence_init_dt() only fills
	 * channels/resolution/oversampling, leaving options and calibrate
	 * untouched. A stack-local left uninitialised gives adc_context a
	 * garbage options pointer (it derefs sequence->options and later
	 * invokes options->callback), which segfaults or trips the
	 * buffer-size check. The real app keeps adc_seq in a zeroed static
	 * struct, so it never hits this. */
	struct adc_sequence seq = {0};
	int rc;

	(void)adc_sequence_init_dt(spec, &seq);
	seq.buffer = out;
	seq.buffer_size = sizeof(*out);

	rc = adc_read_dt(spec, &seq);
	return rc;
}

static struct adc_channel_cfg make_channel_cfg(
	uint8_t channel_id,
	enum adc_gain gain,
	enum adc_reference reference,
	uint16_t acquisition_time,
	bool differential,
	uint8_t input_positive,
	uint8_t input_negative)
{
	struct adc_channel_cfg cfg = {
		.gain = gain,
		.reference = reference,
		.acquisition_time = acquisition_time,
		.channel_id = channel_id,
		.differential = differential,
		.input_positive = input_positive,
		.input_negative = input_negative,
	};

	return cfg;
}

static int read_sequence(uint32_t channels, uint8_t resolution,
			 uint8_t oversampling, size_t buffer_size,
			 const struct adc_sequence_options *options)
{
	int16_t samples[4] = {0};
	struct adc_sequence sequence = {
		.options = options,
		.channels = channels,
		.buffer = samples,
		.buffer_size = buffer_size,
		.resolution = resolution,
		.oversampling = oversampling,
	};

	return adc_read(ch0.dev, &sequence);
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

ZTEST(ads1x1x_multichannel, test_all_differential_and_single_ended_muxes)
{
	static const struct {
		bool differential;
		uint8_t input_positive;
		uint8_t input_negative;
	} mux_cases[] = {
		{ true, 0, 1 },
		{ true, 0, 3 },
		{ true, 1, 3 },
		{ true, 2, 3 },
		{ false, 0, 0 },
		{ false, 1, 0 },
		{ false, 2, 0 },
		{ false, 3, 0 },
	};

	for (size_t i = 0; i < ARRAY_SIZE(mux_cases); i++) {
		struct adc_channel_cfg cfg = make_channel_cfg(
			0, ADC_GAIN_8, ADC_REF_INTERNAL, ADC_ACQ_TIME_DEFAULT,
			mux_cases[i].differential, mux_cases[i].input_positive,
			mux_cases[i].input_negative);

		zassert_ok(adc_channel_setup(ch0.dev, &cfg),
			   "MUX case %zu rejected", i);
	}

	/* Single-ended ADS1115 samples expose 15 bits rather than the 16-bit
	 * differential result. Read the final AIN3 setup to cover that path. */
	zassert_ok(read_sequence(BIT(0), 15, 0, sizeof(int16_t), NULL));
}

ZTEST(ads1x1x_multichannel, test_rejects_invalid_channel_inputs)
{
	struct adc_channel_cfg cfg = make_channel_cfg(
		8, ADC_GAIN_8, ADC_REF_INTERNAL, ADC_ACQ_TIME_DEFAULT,
		true, 0, 1);

	zassert_equal(adc_channel_setup(ch0.dev, &cfg), -EINVAL);

	cfg = make_channel_cfg(0, ADC_GAIN_8, ADC_REF_VDD_1_4,
			       ADC_ACQ_TIME_DEFAULT, true, 0, 1);
	zassert_equal(adc_channel_setup(ch0.dev, &cfg), -ENOTSUP);

	cfg = make_channel_cfg(0, ADC_GAIN_8, ADC_REF_INTERNAL,
			       ADC_ACQ_TIME_DEFAULT, true, 0, 2);
	zassert_equal(adc_channel_setup(ch0.dev, &cfg), -ENOTSUP);

	cfg = make_channel_cfg(0, ADC_GAIN_8, ADC_REF_INTERNAL,
			       ADC_ACQ_TIME_DEFAULT, false, 4, 0);
	zassert_equal(adc_channel_setup(ch0.dev, &cfg), -ENOTSUP);
}

ZTEST(ads1x1x_multichannel, test_accepts_every_data_rate_and_gain)
{
	static const enum adc_gain gains[] = {
		ADC_GAIN_1_3,
		ADC_GAIN_1_2,
		ADC_GAIN_1,
		ADC_GAIN_2,
		ADC_GAIN_4,
		ADC_GAIN_8,
	};

	for (uint16_t data_rate = 0; data_rate <= 7; data_rate++) {
		struct adc_channel_cfg cfg = make_channel_cfg(
			0, ADC_GAIN_8, ADC_REF_INTERNAL,
			ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, data_rate),
			true, 0, 1);

		zassert_ok(adc_channel_setup(ch0.dev, &cfg),
			   "data-rate selector %u rejected", data_rate);
	}

	for (size_t i = 0; i < ARRAY_SIZE(gains); i++) {
		struct adc_channel_cfg cfg = make_channel_cfg(
			0, gains[i], ADC_REF_INTERNAL, ADC_ACQ_TIME_DEFAULT,
			true, 0, 1);

		zassert_ok(adc_channel_setup(ch0.dev, &cfg),
			   "gain index %zu rejected", i);
	}
}

ZTEST(ads1x1x_multichannel, test_rejects_invalid_data_rate_and_gain)
{
	struct adc_channel_cfg cfg = make_channel_cfg(
		0, ADC_GAIN_8, ADC_REF_INTERNAL,
		ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 20), true, 0, 1);

	zassert_equal(adc_channel_setup(ch0.dev, &cfg), -ENOTSUP);

	cfg = make_channel_cfg(0, ADC_GAIN_8, ADC_REF_INTERNAL,
			       ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 8), true, 0, 1);
	zassert_equal(adc_channel_setup(ch0.dev, &cfg), -ENOTSUP);

	cfg = make_channel_cfg(0, ADC_GAIN_16, ADC_REF_INTERNAL,
			       ADC_ACQ_TIME_DEFAULT, true, 0, 1);
	zassert_equal(adc_channel_setup(ch0.dev, &cfg), -ENOTSUP);
}

ZTEST(ads1x1x_multichannel, test_rejects_invalid_sequences)
{
	struct adc_channel_cfg cfg = make_channel_cfg(
		0, ADC_GAIN_8, ADC_REF_INTERNAL, ADC_ACQ_TIME_DEFAULT,
		true, 0, 1);

	zassert_ok(adc_channel_setup(ch0.dev, &cfg));
	zassert_equal(read_sequence(0, 16, 0, sizeof(int16_t), NULL), -ENOTSUP);
	zassert_equal(read_sequence(BIT(0) | BIT(1), 16, 0,
				    sizeof(int16_t), NULL), -ENOTSUP);
	zassert_equal(read_sequence(BIT(6), 16, 0, sizeof(int16_t), NULL),
		      -ENOTSUP);
	zassert_equal(read_sequence(BIT(0), 15, 0, sizeof(int16_t), NULL),
		      -ENOTSUP);
	zassert_equal(read_sequence(BIT(0), 16, 1, sizeof(int16_t), NULL),
		      -ENOTSUP);
	zassert_equal(read_sequence(BIT(0), 16, 0, 1, NULL), -ENOTSUP);
}

ZTEST(ads1x1x_multichannel, test_extra_sampling_uses_full_buffer)
{
	struct adc_channel_cfg cfg = make_channel_cfg(
		0, ADC_GAIN_8, ADC_REF_INTERNAL, ADC_ACQ_TIME_DEFAULT,
		true, 0, 1);
	struct adc_sequence_options options = {
		.extra_samplings = 1,
	};

	zassert_ok(adc_channel_setup(ch0.dev, &cfg));
	zassert_equal(read_sequence(BIT(0), 16, 0, sizeof(int16_t), &options),
		      -ENOTSUP);
	zassert_ok(read_sequence(BIT(0), 16, 0, 2 * sizeof(int16_t), &options));
}

/* A CONFIG write that fails must abort the read: exercises ads1x1x_write_reg's
 * error branch and adc_context_start_sampling's immediate-complete path. */
ZTEST(ads1x1x_multichannel, test_i2c_write_failure_propagates)
{
	int16_t v = 0;

	zassert_ok(adc_channel_setup_dt(&ch0));
	ads1115_emul_fault_reset();
	ads1115_emul_fail_writes(true);

	zassert_not_equal(read_channel(&ch0, &v), 0,
			  "a failed CONFIG write must fail the read");

	ads1115_emul_fault_reset();
	zassert_ok(read_channel(&ch0, &v), "driver must recover after the fault clears");
}

/* A failed status read in wait_data_ready propagates: exercises
 * ads1x1x_read_reg's error branch and the acquisition thread's error path. */
ZTEST(ads1x1x_multichannel, test_i2c_read_failure_in_wait_ready)
{
	int16_t v = 0;

	zassert_ok(adc_channel_setup_dt(&ch0));
	ads1115_emul_fail_read_at(1);   /* first read = CONFIG status */

	zassert_not_equal(read_channel(&ch0, &v), 0,
			  "a failed status read must fail the read");

	ads1115_emul_fault_reset();
	zassert_ok(read_channel(&ch0, &v));
}

/* A failed conversion-register read propagates through adc_perform_read. */
ZTEST(ads1x1x_multichannel, test_i2c_read_failure_in_conversion)
{
	int16_t v = 0;

	zassert_ok(adc_channel_setup_dt(&ch0));
	ads1115_emul_fail_read_at(2);   /* CONFIG ok, CONV read fails */

	zassert_not_equal(read_channel(&ch0, &v), 0,
			  "a failed conversion read must fail the read");

	ads1115_emul_fault_reset();
	zassert_ok(read_channel(&ch0, &v));
}

/* When the OS "ready" bit is not set on the first status read, the driver must
 * poll: exercises the not-ready re-read branch of wait_data_ready. */
ZTEST(ads1x1x_multichannel, test_conversion_not_immediately_ready)
{
	int16_t v = 0;

	zassert_ok(adc_channel_setup_dt(&ch0));
	ads1115_emul_os_busy_reads(2);  /* first two CONFIG reads report "converting" */

	zassert_ok(read_channel(&ch0, &v), "driver must poll until conversion completes");
	ads1115_emul_fault_reset();
	zassert_equal(v, EXPECT_CH0_AIN0_1, "value still correct after polling");
}

ZTEST_SUITE(ads1x1x_multichannel, NULL, NULL, NULL, NULL, NULL);
