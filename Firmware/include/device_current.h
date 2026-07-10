/**
 * @file device_current.h
 * @brief Generic whole-device instantaneous-current API.
 *
 * A board registers exactly one current provider; consumers (e.g. the solenoid
 * closed-loop current check) read through this abstraction without knowing the
 * source. On the Poseidon variant the provider is the battery's DS2782 fuel
 * gauge (CMD 0x06 over I2C); future boards with an on-board solenoid/rail shunt
 * register an ADC-backed provider instead.
 *
 * Unit and sign convention (all providers MUST honour it):
 *   - microamps (µA), signed
 *   - positive = current the device is *drawing* from its supply; a larger
 *     value means more draw. Providers whose native sense is discharge-negative
 *     (DS2782) negate before returning.
 *
 * A provider may decline a reading (return false) when it has no fresh sample
 * or knows the measurement is momentarily untrustworthy; callers treat a
 * decline the same as "no provider": skip whatever the reading would drive.
 */
#ifndef DEVICE_CURRENT_H
#define DEVICE_CURRENT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Provider callback: report the latest whole-device current.
 *
 * @param[out] out_ua  Latest current in µA, positive = draw (see file header).
 * @param[out] age_ms  Milliseconds since the sample was taken.
 * @return true if a trustworthy sample was written; false to decline.
 */
typedef bool (*device_current_provider_fn)(int32_t *out_ua, uint32_t *age_ms);

/**
 * @brief Provider callback: acquire a fresh sample as soon as possible.
 *
 * Best-effort and quick: a consumer that needs a reading synchronised to an
 * event (e.g. the solenoid check, just before energising) calls
 * device_current_trigger() to kick the provider, then reads the fresher value
 * back through device_current_read() shortly after. The provider decides how
 * (the Poseidon provider solicits a DS2782 register read over I2C); it may
 * block briefly on its own bus but must not stall the caller for long, and may
 * do nothing if the acquisition is already in flight or the source is
 * self-refreshing (e.g. a free-running ADC).
 */
typedef void (*device_current_trigger_fn)(void);

/**
 * @brief Register the board's current provider (last registration wins).
 *
 * @param fn Provider callback, or NULL to unregister.
 */
void device_current_register(device_current_provider_fn fn);

/**
 * @brief Register the board's optional fresh-sample trigger (last wins).
 *
 * @param fn Trigger callback, or NULL if the source can't be nudged.
 */
void device_current_register_trigger(device_current_trigger_fn fn);

/**
 * @brief Ask the active provider to acquire a fresh sample now (best-effort).
 *
 * No-op if no trigger is registered. The fresher value becomes visible through
 * device_current_read() once the provider's acquisition completes (which may be
 * asynchronous — e.g. an I2C round-trip), so this does not itself return a value.
 */
void device_current_trigger(void);

/**
 * @brief Read the whole-device instantaneous current from the active provider.
 *
 * @param[out] out_ua Latest current in µA, positive = draw. Untouched on false.
 * @param[out] age_ms Milliseconds since the sample was taken. May be NULL.
 * @return true if a provider is registered and returned a sample; false if no
 *         provider is registered or the provider declined.
 */
bool device_current_read(int32_t *out_ua, uint32_t *age_ms);

#endif /* DEVICE_CURRENT_H */
