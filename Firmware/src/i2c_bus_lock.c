/**
 * @file i2c_bus_lock.c
 * @brief Shared arbitration mutex for STM32-initiated i2c1 master transfers.
 *
 * See i2c_bus_lock.h for the multimaster rationale.
 */

#include "i2c_bus_lock.h"

#include <zephyr/kernel.h>

/* Framework-mandated file-scope object: K_MUTEX_DEFINE places the control
 * block in an iterable section for compile-time static initialisation, the
 * same pattern SonarQube M23_388 accepts for K_THREAD_DEFINE. */
K_MUTEX_DEFINE(i2c1_bus_mutex);

void i2c1_bus_lock(void)
{
    (void)k_mutex_lock(&i2c1_bus_mutex, K_FOREVER);
}

void i2c1_bus_unlock(void)
{
    (void)k_mutex_unlock(&i2c1_bus_mutex);
}
