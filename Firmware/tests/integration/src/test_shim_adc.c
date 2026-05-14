/*
 * ADC shim — provides cell millivolts and battery voltage injection
 * for the integration test harness.
 *
 * The firmware accesses three ADC devices:
 *  - adc0       — internal ADC, channel 0 for battery voltage (through
 *                  a 7.25x external divider per the production hardware)
 *  - adc_ext1   — ADS1115 #1 on hardware, replaced by adc_emul
 *                  channel 0 = cell 1, channel 1 = cell 2
 *  - adc_ext2   — ADS1115 #2 on hardware, replaced by adc_emul
 *                  channel 0 = cell 3
 *
 * On hardware the analog cell driver configures channels with
 * ADC_REF_INTERNAL + ADC_GAIN_8 (±0.256V PGA). We set the emulated
 * reference voltage to 256 mV so adc_emul_const_value_set(mV) maps
 * the injected millivolts onto the 12-bit raw sample correctly.
 *
 * Battery readings go through STM32 internal VREF (3.0V, ADC_GAIN_1,
 * 12-bit). On adc_emul we set the internal reference to 3000 mV and
 * provide set_battery_voltage(V) which inverts the 7.25x divider before
 * calling adc_emul_const_value_set.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>

#include "test_shim_adc.h"
#include "test_shim_shared.h"

LOG_MODULE_REGISTER(test_shim_adc, LOG_LEVEL_INF);

/* ADS1115 with PGA ±0.256V: input range = ±256 mV → ±32767 raw counts. */
#define ADC_EXT_FS_MV    256
#define ADC_EXT_FS_COUNTS 32767

/* STM32L4 internal ADC reference */
#define ADC_INT_REF_MV   3000U

/* Battery divider: 7.25x means battery = pin_voltage * 7.25.
 * To inject a target battery voltage we set the ADC pin to V_batt / 7.25 */
#define BATT_DIVIDER_NUM 1000U
#define BATT_DIVIDER_DEN 7250U

static const struct device *const adc0_dev =
    DEVICE_DT_GET(DT_NODELABEL(adc0));
static const struct device *const adc_ext1_dev =
    DEVICE_DT_GET(DT_NODELABEL(adc_ext1));
static const struct device *const adc_ext2_dev =
    DEVICE_DT_GET(DT_NODELABEL(adc_ext2));

/**
 * Configure ADC emul references so the firmware's ADC reads return
 * sensible values. Must run before the cell threads and power_init
 * call adc_channel_setup().
 */
/* Callback invoked by adc_emul on each adc_read for external cell channels.
 * Reads the millivolt value directly from shared memory and converts to
 * raw counts, making the value available at exactly the point the firmware
 * requests it — no sync timer jitter. */
/* adc_emul calls this on each adc_read().  The firmware configures
 * differential mode, so adc_emul uses the result directly as raw ADC
 * codes (bypassing gain/reference conversion).  Same conversion as
 * shim_adc_set_analog_millis: raw = mV * 32767 / 256. */
static int shm_cell_value_cb(const struct device *dev, unsigned int chan,
                             void *data, uint32_t *result)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    const struct shim_shared_state *sh = shim_shared_get();
    if (sh == NULL) {
        *result = 0;
        return 0;
    }
    unsigned int cell_idx = (unsigned int)(uintptr_t)data;
    float mv = sh->analog_millis[cell_idx];
    if (mv < 0.0f) {
        mv = 0.0f;
    }
    if (mv > (float)ADC_EXT_FS_MV) {
        mv = (float)ADC_EXT_FS_MV;
    }
    *result = (uint32_t)(mv * (float)ADC_EXT_FS_COUNTS / (float)ADC_EXT_FS_MV);
    return 0;
}

static int shim_adc_init(void)
{
    int ret;

    if (!device_is_ready(adc0_dev) ||
        !device_is_ready(adc_ext1_dev) ||
        !device_is_ready(adc_ext2_dev)) {
        return -ENODEV;
    }

    ret = adc_emul_ref_voltage_set(adc0_dev, ADC_REF_INTERNAL, ADC_INT_REF_MV);
    if (ret != 0) {
        LOG_ERR("adc0 ref set failed: %d", ret);
        return ret;
    }

    (void)adc_emul_ref_voltage_set(adc_ext1_dev, ADC_REF_INTERNAL, ADC_EXT_FS_MV);
    (void)adc_emul_ref_voltage_set(adc_ext2_dev, ADC_REF_INTERNAL, ADC_EXT_FS_MV);

    (void)adc_emul_raw_value_func_set(adc_ext1_dev, 0, shm_cell_value_cb, (void *)0);
    (void)adc_emul_raw_value_func_set(adc_ext1_dev, 1, shm_cell_value_cb, (void *)1);
    (void)adc_emul_raw_value_func_set(adc_ext2_dev, 0, shm_cell_value_cb, (void *)2);

    /* Default battery voltage: 7.4V (2S lithium nominal, well above
     * the 6.0V LI2S threshold so low_battery starts FALSE) */
    (void)shim_adc_set_battery_voltage(7.4f);

    return 0;
}

/* Run after the ADC devices initialize (CONFIG_ADC_INIT_PRIORITY=50)
 * and before the power subsystem (POST_KERNEL priority 91). */
SYS_INIT(shim_adc_init, POST_KERNEL, 60);

int shim_adc_set_analog_millis(uint8_t cell, float millis)
{
    /* Keep shared memory in sync so the raw_value_func callback
     * (registered during init) returns the correct value regardless
     * of whether the caller is the socket handler or direct code. */
    struct shim_shared_state *sh = shim_shared_get();
    if (sh != NULL && cell >= 1 && cell <= 3) {
        sh->analog_millis[cell - 1] = millis;
    }

    const struct device *dev;
    unsigned int chan;

    /* Cells are 1-indexed externally, map to ADS1115 instance + channel:
     *   cell 1 → adc_ext1 ch 0
     *   cell 2 → adc_ext1 ch 1
     *   cell 3 → adc_ext2 ch 0
     */
    switch (cell) {
    case 1:
        dev = adc_ext1_dev;
        chan = 0;
        break;
    case 2:
        dev = adc_ext1_dev;
        chan = 1;
        break;
    case 3:
        dev = adc_ext2_dev;
        chan = 0;
        break;
    default:
        return -EINVAL;
    }

    /* Convert input mV directly to ADS1115 raw counts: 256 mV FS maps
     * to 32767 raw, so raw = mV * 32767 / 256.  Bypasses adc_emul's
     * gain/reference math, which doesn't match the ADS1115's PGA
     * semantics (Zephyr treats GAIN_8 as 8× input amplification while
     * the ADS1115 uses it to select narrower input range). */
    float v = millis < 0.0f ? 0.0f : millis;
    if (v > (float)ADC_EXT_FS_MV) {
        v = (float)ADC_EXT_FS_MV;
    }
    uint32_t raw = (uint32_t)(v * (float)ADC_EXT_FS_COUNTS / (float)ADC_EXT_FS_MV);

    int ret = adc_emul_const_raw_value_set(dev, chan, raw);
    if (ret != 0) {
        LOG_WRN("adc cell %u set failed: %d", cell, ret);
    }
    return ret;
}

int shim_adc_set_battery_voltage(float volts)
{
    if (volts < 0.0f) {
        volts = 0.0f;
    }

    /* Battery voltage divided by 7.25x reaches the ADC pin */
    float pin_mv = volts * 1000.0f * (float)BATT_DIVIDER_NUM /
                   (float)BATT_DIVIDER_DEN;
    if (pin_mv > (float)ADC_INT_REF_MV) {
        pin_mv = (float)ADC_INT_REF_MV;
    }

    return adc_emul_const_value_set(adc0_dev, 0, (uint32_t)pin_mv);
}
