/**
 * @file i2c_bus_lock.c
 * @brief Shared arbitration mutex for STM32-initiated i2c1 master transfers.
 *
 * See i2c_bus_lock.h for the multimaster rationale.
 */

#include "i2c_bus_lock.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/irq.h>

#include <errno.h>

/* PE must stay low this long for the I2Cv2 state-machine reset to take; the
 * reference manual requires >= 3 APB cycles, 2 us is comfortably above that at
 * any supported PCLK1. */
#define I2C1_PE_RESET_HOLD_US 2

/* Framework-mandated file-scope object: K_MUTEX_DEFINE places the control
 * block in an iterable section for compile-time static initialisation, the
 * same pattern SonarQube M23_388 accepts for K_THREAD_DEFINE. */
K_MUTEX_DEFINE(i2c1_bus_mutex);

/* Resolve the i2c1 controller at build time where it exists (all real
 * hardware variants); NULL on topologies without the node (native_sim uses
 * i2c0) so i2c1_bus_recover() degrades to -ENODEV instead of failing to link. */
#if DT_NODE_EXISTS(DT_NODELABEL(i2c1))
static const struct device *const i2c1_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
#else
static const struct device *const i2c1_dev;
#endif

#if defined(CONFIG_SOC_FAMILY_STM32) && DT_NODE_EXISTS(DT_NODELABEL(i2c1))
#include <stm32_ll_i2c.h>

/**
 * @brief Clear a latched STM32 hardware BUSY flag by pulsing peripheral-enable.
 *
 * STM32 I2Cv2 clears BUSY only on a STOP it observes on the wire or on PE=0.
 * While an i2c target is registered the Zephyr driver keeps PE high permanently
 * (it skips the between-transfer LL_I2C_Disable), so a BUSY latched by a
 * multimaster collision never self-clears, and bit-bang recovery — which muxes
 * the pins away from the peripheral — can't reach it. Pulsing PE resets the I2C
 * state machine while retaining the OAR1/CR1 configuration, so the Poseidon
 * target stays armed across the pulse. irq_lock keeps disable+enable atomic so
 * the target is never deaf for a scheduling quantum.
 *
 * Poking the peripheral behind the Zephyr driver is deliberate: the driver
 * exposes no PE-reset, and the vendored zephyr is a west clone we don't patch
 * (see COMPROMISE.md). Caller must hold i2c1_bus_lock().
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

void i2c1_bus_unlock(void)
{
    (void)k_mutex_unlock(&i2c1_bus_mutex);
}

int i2c1_bus_recover(void)
{
    int ret = -ENODEV;

    if (i2c1_dev != NULL) {
        i2c1_bus_lock();
        /* Two failure modes, two remedies: bit-bang a STOP to release a slave
         * physically holding SDA, then pulse PE to clear a peripheral BUSY
         * latch that the bit-bang can't reach. */
        ret = i2c_recover_bus(i2c1_dev);
        i2c1_reset_peripheral();
        i2c1_bus_unlock();
    }

    return ret;
}
