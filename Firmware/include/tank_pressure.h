/**
 * @file tank_pressure.h
 * @brief HP tank pressure transducer types, zbus channel, and pure math.
 *
 * Analog HP transducers (O2 and/or diluent) are wired to spare analog-cell
 * ADC channels, selected per variant via CONFIG_O2_TRANSDUCER_CHANNEL /
 * CONFIG_DIL_TRANSDUCER_CHANNEL. The sampler thread in tank_pressure.c
 * publishes TankPressureMsg_t on chan_tank_pressure; the DiveCAN PPO2 TX
 * thread relays the values to the handset as TANK_PRESSURE_ID frames
 * (decibar, per DiveCAN Messaging/Pressure.md).
 */
#ifndef TANK_PRESSURE_H
#define TANK_PRESSURE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef CONFIG_ZBUS
#include <zephyr/zbus/zbus.h>
#endif

/** @brief Tank pressure in decibar (DiveCAN wire units; 515 = 51.5 bar). */
typedef uint16_t TankPressure_t;

/** @brief Transducer output voltage in millivolts (signed: a differential
 *         ADC read below the negative input yields a negative value). */
typedef int32_t TransducerMv_t;

/** @brief Full-scale transducer pressure in bar (Kconfig *_TRANSDUCER_LIMIT). */
typedef uint32_t TransducerLimitBar_t;

/** @brief Wire/channel sentinel for a failed or out-of-range reading. */
#define TANK_PRESSURE_FAIL 0xFFFFU

/** @brief Tank pressures published on chan_tank_pressure.
 *
 * A cylinder whose transducer is not configured, is out of its valid
 * millivolt range, or whose ADC read failed carries TANK_PRESSURE_FAIL.
 */
typedef struct {
    TankPressure_t o2_decibar;   /**< O2 cylinder pressure in decibar */
    TankPressure_t dil_decibar;  /**< Diluent cylinder pressure in decibar */
    int64_t timestamp_ticks;     /**< k_uptime_ticks() at sample time */
} TankPressureMsg_t;

/**
 * @brief Map a transducer voltage to tank pressure in decibar.
 *
 * Linear mapping: min_mv -> 0 bar, max_mv -> limit_bar. Readings outside
 * [min_mv, max_mv] (open/shorted sensor), a non-positive span
 * (misconfiguration), or a result that does not fit the wire field all
 * yield TANK_PRESSURE_FAIL. Result is rounded to the nearest decibar.
 *
 * @param millivolts Measured transducer output in millivolts.
 * @param min_mv     Output at 0 bar (CONFIG_*_TRANSDUCER_MIN).
 * @param max_mv     Output at full scale (CONFIG_*_TRANSDUCER_MAX).
 * @param limit_bar  Full-scale pressure in bar (CONFIG_*_TRANSDUCER_LIMIT).
 * @return Pressure in decibar, or TANK_PRESSURE_FAIL.
 */
TankPressure_t tank_pressure_mv_to_decibar(TransducerMv_t millivolts,
                                           TransducerMv_t min_mv,
                                           TransducerMv_t max_mv,
                                           TransducerLimitBar_t limit_bar);

#ifdef CONFIG_ZBUS
ZBUS_CHAN_DECLARE(chan_tank_pressure);
#endif

#endif /* TANK_PRESSURE_H */
