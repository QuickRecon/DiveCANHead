#ifndef SOLENOID_H
#define SOLENOID_H

#include <zephyr/device.h>
#include <stdint.h>

/**
 * @brief Fire a solenoid for a given duration.
 *
 * Opens the solenoid GPIO and arms the hardware deadman timer.
 * When the timer expires, ALL solenoids on this device are
 * forced off by the counter ISR — no kernel involvement.
 *
 * If called while the timer is already running, the timeout is
 * reset to the new duration.
 *
 * @param dev     Solenoid device (from devicetree)
 * @param channel Solenoid index (0-based)
 * @param duration_us On-time in microseconds (clamped to max-on-time-us)
 * @return 0 on success, negative errno on failure
 */
int solenoid_fire(const struct device *dev, uint8_t channel,
          uint32_t duration_us);

/**
 * @brief Turn off a single solenoid immediately.
 *
 * Does not cancel the deadman timer — other solenoids may
 * still be active.
 */
void solenoid_off(const struct device *dev, uint8_t channel);

/**
 * @brief Emergency shutdown — cancel timer and force all solenoids off.
 */
void solenoid_all_off(const struct device *dev);

/**
 * @brief Return the number of solenoid channels on this device.
 */
uint8_t solenoid_channel_count(const struct device *dev);

#endif
