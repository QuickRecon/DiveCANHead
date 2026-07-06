/**
 * @file i2c_bus_lock.h
 * @brief Application-level arbitration lock for the shared i2c1 controller.
 *
 * On the Poseidon variant the STM32 drives i2c1 as a multi-master + target
 * bus: the tank-pressure sampler reads the ADS1115 transducers (bus master)
 * while poseidon_accessories drives the HUD/battery/display (bus master) and
 * answers the external Poseidon display (bus target). When two STM32-initiated
 * master transfers race, the second one finds the controller BUSY and fails
 * with -EBUSY — observed as "ADS1X1X: error writing register 0x1 (-16)" from
 * the ADS conversion-trigger write colliding with a Poseidon output frame.
 *
 * This mutex serialises the STM32's own master transfers so they never race
 * each other: every application call site that starts a controller transaction
 * on i2c1 wraps it in i2c1_bus_lock() / i2c1_bus_unlock(). It deliberately
 * cannot arbitrate against the *external* Poseidon masters (the display writing
 * into our target address, or another head on the bus) — that residual, truly
 * multi-master contention is handled by bounded retry at the call site.
 *
 * The translation unit is unconditional and depends on nothing but the kernel,
 * so it builds for native_sim and is harmless on variants that do not share
 * i2c1 (the lock is simply never contended).
 */
#ifndef I2C_BUS_LOCK_H
#define I2C_BUS_LOCK_H

/**
 * @brief Acquire exclusive use of the i2c1 controller for a master transfer.
 *
 * Blocks until the lock is free. Must be paired with i2c1_bus_unlock().
 */
void i2c1_bus_lock(void);

/**
 * @brief Acquire i2c1 only after the physical bus has been quiet.
 *
 * This is the entry point for low-priority ADS1115 sampling. It acquires the
 * application mutex, then requires SCL/SDA to remain high and no recently
 * completed Poseidon frame to have been observed for a short guard interval.
 * On success the caller owns the mutex and must call i2c1_bus_unlock().
 *
 * @return 0 on success, or -EBUSY if no quiet window appeared before the
 *         bounded timeout. On error the mutex is not held.
 */
int i2c1_bus_lock_quiet(void);

/**
 * @brief Release the i2c1 controller acquired via i2c1_bus_lock().
 */
void i2c1_bus_unlock(void);

/**
 * @brief Record completion of a Poseidon frame.
 *
 * Safe from an I2C target callback. ADS1115 sampling uses this timestamp to
 * avoid starting immediately after an external frame or a local output group.
 */
void i2c1_bus_note_activity(void);

/**
 * @brief Recover a wedged i2c1 bus without disturbing live bus traffic.
 *
 * With a Poseidon i2c target registered the STM32 keeps the I2C peripheral
 * enabled between transfers, so a BUSY flag latched by a multimaster collision
 * can survive after the physical lines return idle. If both lines are stably
 * high, recovery only pulses the STM32 PE bit. Nine-clock physical bus clear is
 * reserved for a confirmed SCL-high/SDA-low stuck condition; active/toggling
 * traffic is never bit-banged. Takes i2c1_bus_lock() internally.
 *
 * @return 0 on success; -ENODEV if i2c1 is absent from the devicetree (e.g.
 *         native_sim); -EBUSY if the bus remains active or SCL is stuck low;
 *         -ENOSYS if bus recovery is unavailable; or a driver error code.
 */
int i2c1_bus_recover(void);

#endif /* I2C_BUS_LOCK_H */
