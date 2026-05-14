#ifndef TEST_SHIM_SHARED_H_
#define TEST_SHIM_SHARED_H_

#include <stdint.h>

#define SHIM_SHM_NAME "/divecan_shim"

struct shim_shared_state {
    /* Python → Firmware */
    float    digital_ppo2[3];    /* bar, per cell (0-indexed) */
    float    analog_millis[3];   /* mV, per cell (0-indexed) */
    float    battery_voltage;    /* V */
    uint8_t  bus_active;         /* 0 = off, 1 = on */
    uint8_t  digital_mode[3];   /* 0 = DiveO2, 1 = O2S (per cell) */

    /* Firmware → Python */
    uint64_t uptime_us;
    int32_t  solenoids[4];
};

struct shim_shared_state *shim_shared_get(void);

#endif /* TEST_SHIM_SHARED_H_ */
