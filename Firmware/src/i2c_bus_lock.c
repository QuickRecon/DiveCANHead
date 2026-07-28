/**
 * @file i2c_bus_lock.c
 * @brief Shared arbitration mutex for STM32-initiated i2c1 master transfers.
 *
 * See i2c_bus_lock.h for the multimaster rationale.
 */

#include "i2c_bus_lock.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>

#include <errno.h>

#if defined(CONFIG_SOC_FAMILY_STM32) && DT_NODE_EXISTS(DT_NODELABEL(i2c1))
#include <stm32_ll_i2c.h>
#endif

LOG_MODULE_REGISTER(i2c_bus_lock, LOG_LEVEL_INF);

/* PE must stay low this long for the I2Cv2 state-machine reset to take; the
 * reference manual requires >= 3 APB cycles, 2 us is comfortably above that at
 * any supported PCLK1. */
#define I2C1_PE_RESET_HOLD_US 2
/* Poseidon devices emit short frame groups. Requiring a stable idle interval
 * longer than the observed ~1.6 ms inter-frame spacing avoids inserting an ADS
 * transaction into the middle of a group. */
#define I2C1_QUIET_GUARD_MS 3U
#define I2C1_QUIET_TIMEOUT_MS 25U
/* A physical line must remain in the same non-idle state for this long before
 * it is declared stuck. A valid 100 kHz Poseidon frame is below 1 ms. */
#define I2C1_STUCK_CONFIRM_MS 25U
#define I2C1_CLASSIFY_TIMEOUT_MS 30U
#define I2C1_LINE_POLL_US 100

/* Framework-mandated file-scope object: K_MUTEX_DEFINE places the control
 * block in an iterable section for compile-time static initialisation, the
 * same pattern SonarQube M23_388 accepts for K_THREAD_DEFINE. */
K_MUTEX_DEFINE(i2c1_bus_mutex);

/**
 * @brief Return a pointer to the module-static last-bus-activity timestamp.
 *
 * @return Pointer to the atomic_t holding the millisecond uptime of the
 *         last observed i2c1 transfer.
 */
static atomic_t *getLastBusActivityMs(void)
{
    static atomic_t last_bus_activity_ms;

    return &last_bus_activity_ms;
}

/* Resolve the i2c1 controller at build time where it exists (all real
 * hardware variants); NULL on topologies without the node (native_sim uses
 * i2c0) so i2c1_bus_recover() degrades to -ENODEV instead of failing to link. */
#if DT_NODE_EXISTS(DT_NODELABEL(i2c1))
static const struct device *const i2c1_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
#else
static const struct device *const i2c1_dev;
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(i2c1)) && \
    DT_NODE_HAS_PROP(DT_NODELABEL(i2c1), scl_gpios) && \
    DT_NODE_HAS_PROP(DT_NODELABEL(i2c1), sda_gpios)
#define I2C1_HAS_LINE_GPIOS 1
static const struct gpio_dt_spec i2c1_scl =
    GPIO_DT_SPEC_GET(DT_NODELABEL(i2c1), scl_gpios);
static const struct gpio_dt_spec i2c1_sda =
    GPIO_DT_SPEC_GET(DT_NODELABEL(i2c1), sda_gpios);
#else
#define I2C1_HAS_LINE_GPIOS 0
#endif

enum i2c1_line_state {
    I2C1_LINES_IDLE,
    I2C1_LINES_SDA_STUCK,
    I2C1_LINES_SCL_STUCK,
    I2C1_LINES_ACTIVE,
};

static bool i2c1_get_lines(bool *scl_high, bool *sda_high)
{
    bool result = true;

#if I2C1_HAS_LINE_GPIOS
    Status_t scl = gpio_pin_get_dt(&i2c1_scl);
    Status_t sda = gpio_pin_get_dt(&i2c1_sda);

    if ((scl < 0) || (sda < 0)) {
        result = false;
    } else {
        *scl_high = scl != 0;
        *sda_high = sda != 0;
    }
#else
    *scl_high = true;
    *sda_high = true;
#endif

    return result;
}

static bool i2c1_activity_guard_elapsed(void)
{
    uint32_t now = k_uptime_get_32();
    uint32_t last = (uint32_t)atomic_get(getLastBusActivityMs());

    return (now - last) >= I2C1_QUIET_GUARD_MS;
}

/**
 * Require both physical lines to remain high for the quiet guard interval.
 * Caller holds i2c1_bus_mutex, preventing another local controller transfer
 * from occupying the gap while it is being measured.
 */
static Status_t i2c1_wait_quiet(void)
{
    Status_t result = -EBUSY;
    uint32_t started = k_uptime_get_32();
    uint32_t idle_since = started;
    bool measuring_idle = false;

    while ((result == -EBUSY) &&
           ((k_uptime_get_32() - started) < I2C1_QUIET_TIMEOUT_MS)) {
        bool scl_high = false;
        bool sda_high = false;
        uint32_t now = k_uptime_get_32();

        if (!i2c1_get_lines(&scl_high, &sda_high)) {
            result = -EIO;
        }
        else if (scl_high && sda_high && i2c1_activity_guard_elapsed()) {
            if (!measuring_idle) {
                idle_since = now;
                measuring_idle = true;
            } else if ((now - idle_since) >= I2C1_QUIET_GUARD_MS) {
                result = 0;
            } else {
                /* Still within the guard interval — keep measuring. */
            }
        }
        else {
            measuring_idle = false;
        }

        if (result == -EBUSY) {
            (void)k_usleep(I2C1_LINE_POLL_US);
        }
    }

    return result;
}

/**
 * Classify a persistent physical-line fault. Transitions reset the confirmation
 * timer, so active bus traffic can time out as ACTIVE but can never be mistaken
 * for an SDA-stuck condition and overwritten by bit-bang recovery.
 */
static enum i2c1_line_state i2c1_classify_lines(void)
{
    enum i2c1_line_state result = I2C1_LINES_ACTIVE;
    uint32_t started = k_uptime_get_32();
    uint32_t state_since = started;
    bool previous_scl = false;
    bool previous_sda = false;
    bool have_previous = false;
    bool done = false;

    while ((!done) &&
           ((k_uptime_get_32() - started) < I2C1_CLASSIFY_TIMEOUT_MS)) {
        bool scl_high = false;
        bool sda_high = false;
        uint32_t now = k_uptime_get_32();
        uint32_t stable_ms = 0U;

        if (!i2c1_get_lines(&scl_high, &sda_high)) {
            /* result stays I2C1_LINES_ACTIVE */
            done = true;
        }
        else {
            if ((!have_previous) || (scl_high != previous_scl) ||
                (sda_high != previous_sda)) {
                previous_scl = scl_high;
                previous_sda = sda_high;
                state_since = now;
                have_previous = true;
            }

            stable_ms = now - state_since;
            if (scl_high && sda_high && (stable_ms >= I2C1_QUIET_GUARD_MS)) {
                result = I2C1_LINES_IDLE;
                done = true;
            }
            else if (stable_ms >= I2C1_STUCK_CONFIRM_MS) {
                if (scl_high) {
                    result = I2C1_LINES_SDA_STUCK;
                } else {
                    result = I2C1_LINES_SCL_STUCK;
                }
                done = true;
            }
            else {
                /* Not yet stable long enough to classify — keep polling. */
            }

            if (!done) {
                (void)k_usleep(I2C1_LINE_POLL_US);
            }
        }
    }

    return result;
}

#if defined(CONFIG_SOC_FAMILY_STM32) && DT_NODE_EXISTS(DT_NODELABEL(i2c1))

/**
 * @brief Clear a latched STM32 hardware BUSY flag by pulsing peripheral-enable.
 *
 * STM32 I2Cv2 clears BUSY only on a STOP it observes on the wire or on PE=0.
 * While an I2C target is registered, the Zephyr driver keeps PE high between
 * controller transfers, so a BUSY latched by a multimaster collision can
 * survive after the wires return idle. Pulsing PE resets the peripheral state
 * machine while retaining OAR1 and the target interrupt configuration.
 *
 * irq_lock() makes the pulse atomic with respect to the I2C ISR. The upstream
 * controller/target race backport prevents an own-address match from entering
 * the wrong event path; this pulse is the last-resort recovery for hardware
 * state that remains latched after that transaction has ended.
 *
 * Direct PE control is deliberate because Zephyr 4.4.1 exposes no public
 * equivalent that keeps the target registered continuously. Caller holds
 * i2c1_bus_mutex and has already classified the physical line state.
 */
static void i2c1_reset_peripheral(void)
{
    I2C_TypeDef *i2c = (I2C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i2c1));
    unsigned int key = irq_lock();

    LL_I2C_Disable(i2c);
    k_busy_wait(I2C1_PE_RESET_HOLD_US);
    LL_I2C_Enable(i2c);

    irq_unlock(key);
}
#else
static void i2c1_reset_peripheral(void)
{
    /* No STM32 peripheral to reset (e.g. native_sim). */
}
#endif

void i2c1_bus_lock(void)
{
    (void)k_mutex_lock(&i2c1_bus_mutex, K_FOREVER);
}

Status_t i2c1_bus_lock_quiet(void)
{
    Status_t ret = 0;

    i2c1_bus_lock();
    ret = i2c1_wait_quiet();

    if (ret != 0) {
        i2c1_bus_unlock();
    }
    return ret;
}

void i2c1_bus_unlock(void)
{
    (void)k_mutex_unlock(&i2c1_bus_mutex);
}

void i2c1_bus_note_activity(void)
{
    (void)atomic_set(getLastBusActivityMs(), (atomic_val_t)k_uptime_get_32());
}

Status_t i2c1_bus_recover(void)
{
    Status_t ret = -ENODEV;
    enum i2c1_line_state state = I2C1_LINES_ACTIVE;

    if (i2c1_dev != NULL) {
        i2c1_bus_lock();
        state = i2c1_classify_lines();

        if (state == I2C1_LINES_IDLE) {
            /* The wire is idle, so BUSY exists only in the STM32 state machine.
             * Do not disturb external devices with unnecessary clock pulses. */
            LOG_WRN("clearing peripheral BUSY on idle bus");
            i2c1_reset_peripheral();
            ret = 0;
        } else if (state == I2C1_LINES_SDA_STUCK) {
            /* NXP UM10204 bus-clear procedure: only clock a bus whose SCL has
             * remained high while SDA has remained low. */
            LOG_ERR("SDA stuck low; attempting physical bus clear");
            ret = i2c_recover_bus(i2c1_dev);
            i2c1_reset_peripheral();
        } else if (state == I2C1_LINES_SCL_STUCK) {
            /* The STM32 may itself be stretching SCL after a target/controller
             * race. Release its state machine, but never drive clocks into a
             * line another device is holding low. */
            LOG_ERR("SCL stuck low; resetting peripheral without bus clear");
            i2c1_reset_peripheral();
            ret = -EBUSY;
        } else {
            /* Line transitions were still occurring: this is live traffic,
             * not a wedged target. Defer rather than corrupting the frame. */
            LOG_WRN("recovery deferred while bus remains active");
            ret = -EBUSY;
        }
        i2c1_bus_unlock();
    }

    return ret;
}

bool i2c1_error_is_retryable(Status_t rc)
{
    /* The synchronous Zephyr API documents -EIO for general controller
     * failures, including causes that are transient on a multi-master bus.
     * Retry the transport-level results; argument/device errors remain hard. */
    return (rc == -EBUSY) || (rc == -EAGAIN) ||
           (rc == -ETIMEDOUT) || (rc == -EIO);
}

static Status_t i2c1_try_once(I2c1XferFn_t xfer, void *ctx)
{
    Status_t ret = i2c1_bus_lock_quiet();

    if (ret == 0) {
        ret = xfer(ctx);
        i2c1_bus_note_activity();
        i2c1_bus_unlock();
    }
    return ret;
}

Status_t i2c1_transact(I2c1XferFn_t xfer, void *ctx, uint8_t attempts,
          uint32_t backoff_base_ms, uint32_t backoff_jitter_ms)
{
    Status_t ret = -EBUSY;

    if ((xfer == NULL) || (attempts == 0U)) {
        ret = -EINVAL;
    }
    else {
        uint8_t a = 0U;

        while ((a < attempts) && i2c1_error_is_retryable(ret)) {
            if (a > 0U) {
                uint32_t delay = backoff_base_ms << (a - 1U);
                uint32_t jitter = 0U;
                if (backoff_jitter_ms != 0U) {
                    jitter = k_cycle_get_32() % backoff_jitter_ms;
                }
                const uint32_t total_delay_ms = delay + jitter;
                (void)k_msleep((int32_t)total_delay_ms);
            }
            ret = i2c1_try_once(xfer, ctx);
            ++a;
        }

        /* Still wedged after every avoid+retry: classify the lines and recover
         * (idle -> PE reset; SDA-stuck -> nine-clock bus clear; live traffic
         * left alone), then make one final attempt rather than surfacing the
         * failure. */
        if (i2c1_error_is_retryable(ret) && (i2c1_bus_recover() == 0)) {
            ret = i2c1_try_once(xfer, ctx);
        }
    }

    return ret;
}
