/**
 * @file solenoid_roles.h
 * @brief Named solenoid role helpers (O2 inject, O2 flush, diluent flush).
 *
 * Thin inline wrappers around solenoid_fire() / solenoid_off() that map
 * Kconfig channel numbers to semantic role names.  Roles absent on a
 * given variant (channel == SOL_ROLE_NOT_PRESENT) return -ENODEV.
 *
 * On variants with no solenoid hardware at all (CONFIG_SOLENOID=n, e.g.
 * a manual CCR) every role is absent: the helpers are stubs that return
 * -ENODEV / no-op, so callers (PPO2 control, calibration, UDS) still
 * compile and link without the solenoid driver.
 */
#ifndef SOLENOID_ROLES_H
#define SOLENOID_ROLES_H

#include <errno.h>
#include <zephyr/sys/util.h>
#include <autoconf.h>

/** @brief Sentinel value for a solenoid role that is not wired on this variant. */
#define SOL_ROLE_NOT_PRESENT (-1)

#ifdef CONFIG_SOLENOID

#include <solenoid.h>
#include <zephyr/devicetree.h>

#define SOL_DEVICE DEVICE_DT_GET(DT_NODELABEL(solenoids))

/**
 * @brief Fire the O2 injection solenoid for the given duration.
 *
 * @param duration_us On-time in microseconds (clamped to max-on-time-us)
 * @return 0 on success, negative errno on failure
 */
static inline int sol_o2_inject_fire(uint32_t duration_us)
{
    return solenoid_fire(SOL_DEVICE,
                 CONFIG_SOL_O2_INJECT_CHANNEL, duration_us);
}

/**
 * @brief Turn off the O2 injection solenoid immediately.
 */
static inline void sol_o2_inject_off(void)
{
    solenoid_off(SOL_DEVICE, CONFIG_SOL_O2_INJECT_CHANNEL);
}

/**
 * @brief Fire the O2 flush solenoid for the given duration.
 *
 * @param duration_us On-time in microseconds
 * @return 0 on success, -ENODEV if role not present on this variant, negative errno on failure
 */
static inline int sol_o2_flush_fire(uint32_t duration_us)
{
#if CONFIG_SOL_O2_FLUSH_CHANNEL != SOL_ROLE_NOT_PRESENT
    return solenoid_fire(SOL_DEVICE,
                 CONFIG_SOL_O2_FLUSH_CHANNEL, duration_us);
#else
    ARG_UNUSED(duration_us);
    return -ENODEV;
#endif
}

/**
 * @brief Turn off the O2 flush solenoid immediately.
 *
 * No-op if the role is not present on this variant.
 */
static inline void sol_o2_flush_off(void)
{
#if CONFIG_SOL_O2_FLUSH_CHANNEL != SOL_ROLE_NOT_PRESENT
    solenoid_off(SOL_DEVICE, CONFIG_SOL_O2_FLUSH_CHANNEL);
#endif
}

/**
 * @brief Fire the diluent flush solenoid for the given duration.
 *
 * @param duration_us On-time in microseconds
 * @return 0 on success, -ENODEV if role not present on this variant, negative errno on failure
 */
static inline int sol_dil_flush_fire(uint32_t duration_us)
{
#if CONFIG_SOL_DIL_FLUSH_CHANNEL != SOL_ROLE_NOT_PRESENT
    return solenoid_fire(SOL_DEVICE,
                 CONFIG_SOL_DIL_FLUSH_CHANNEL, duration_us);
#else
    ARG_UNUSED(duration_us);
    return -ENODEV;
#endif
}

/**
 * @brief Turn off the diluent flush solenoid immediately.
 *
 * No-op if the role is not present on this variant.
 */
static inline void sol_dil_flush_off(void)
{
#if CONFIG_SOL_DIL_FLUSH_CHANNEL != SOL_ROLE_NOT_PRESENT
    solenoid_off(SOL_DEVICE, CONFIG_SOL_DIL_FLUSH_CHANNEL);
#endif
}

/**
 * @brief Fire the secondary O2 injection solenoid for the given duration.
 *
 * @param duration_us On-time in microseconds
 * @return 0 on success, -ENODEV if role not present on this variant, negative errno on failure
 */
static inline int sol_o2_inject_2_fire(uint32_t duration_us)
{
#if CONFIG_SOL_O2_INJECT_2_CHANNEL != SOL_ROLE_NOT_PRESENT
    return solenoid_fire(SOL_DEVICE,
                 CONFIG_SOL_O2_INJECT_2_CHANNEL, duration_us);
#else
    ARG_UNUSED(duration_us);
    return -ENODEV;
#endif
}

/**
 * @brief Turn off the secondary O2 injection solenoid immediately.
 *
 * No-op if the role is not present on this variant.
 */
static inline void sol_o2_inject_2_off(void)
{
#if CONFIG_SOL_O2_INJECT_2_CHANNEL != SOL_ROLE_NOT_PRESENT
    solenoid_off(SOL_DEVICE, CONFIG_SOL_O2_INJECT_2_CHANNEL);
#endif
}

#else /* !CONFIG_SOLENOID — no solenoid hardware on this variant */

static inline int sol_o2_inject_fire(uint32_t duration_us)
{
    ARG_UNUSED(duration_us);
    return -ENODEV;
}

static inline void sol_o2_inject_off(void)
{
}

static inline int sol_o2_flush_fire(uint32_t duration_us)
{
    ARG_UNUSED(duration_us);
    return -ENODEV;
}

static inline void sol_o2_flush_off(void)
{
}

static inline int sol_dil_flush_fire(uint32_t duration_us)
{
    ARG_UNUSED(duration_us);
    return -ENODEV;
}

static inline void sol_dil_flush_off(void)
{
}

static inline int sol_o2_inject_2_fire(uint32_t duration_us)
{
    ARG_UNUSED(duration_us);
    return -ENODEV;
}

static inline void sol_o2_inject_2_off(void)
{
}

#endif /* CONFIG_SOLENOID */

#endif
