#ifndef SOLENOID_ROLES_H
#define SOLENOID_ROLES_H

#include <solenoid.h>
#include <zephyr/devicetree.h>
#include <autoconf.h>

#define SOL_DEVICE DEVICE_DT_GET(DT_NODELABEL(solenoids))

#define SOL_ROLE_NOT_PRESENT (-1)

static inline int sol_o2_inject_fire(uint32_t duration_us)
{
    return solenoid_fire(SOL_DEVICE,
                 CONFIG_SOL_O2_INJECT_CHANNEL, duration_us);
}

static inline void sol_o2_inject_off(void)
{
    solenoid_off(SOL_DEVICE, CONFIG_SOL_O2_INJECT_CHANNEL);
}

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

#endif
