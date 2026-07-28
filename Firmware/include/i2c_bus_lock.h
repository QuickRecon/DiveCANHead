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

#include <stdbool.h>
#include <stdint.h>

#include "common.h"

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
Status_t i2c1_bus_lock_quiet(void);

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
Status_t i2c1_bus_recover(void);

/**
 * @brief True if @p rc is a transient i2c1 error worth retrying (arbitration
 * loss / BUSY / bus glitch on the multimaster bus) rather than a hard fault.
 */
bool i2c1_error_is_transient(Status_t rc);

/**
 * @brief Unified multimaster-safe i2c1 transfer: avoid + retry + recover.
 *
 * One place for the whole defense so every i2c1 caller (accessory writes, ADS
 * reads, …) handles collisions the same way instead of each re-implementing a
 * subset: up to @p attempts tries of (wait for a quiet bus → run @p xfer),
 * exponential backoff + jitter between tries, and — if the bus is still wedged
 * after them — one classify+recover (PE reset / bus clear) followed by a final
 * try. Serialises against the other STM32 i2c1 masters via the bus mutex and
 * timestamps bus activity on each attempt.
 *
 * @param xfer  Performs the actual transfer; returns 0 or a negative errno.
 *              Only transient errors (see i2c1_error_is_transient) are retried.
 * @param ctx   Opaque pointer passed through to @p xfer.
 * @param attempts          Max quiet-wait+xfer tries before recovery (>= 1).
 * @param backoff_base_ms   Backoff = base << (attempt-1) + jitter.
 * @param backoff_jitter_ms Upper bound of the random jitter added each backoff.
 * @return 0 on success, or the last @p xfer / lock error.
 */
typedef Status_t (*I2c1XferFn_t)(void *ctx);

Status_t i2c1_transact(I2c1XferFn_t xfer, void *ctx, uint8_t attempts,
          uint32_t backoff_base_ms, uint32_t backoff_jitter_ms);

#endif /* I2C_BUS_LOCK_H */
