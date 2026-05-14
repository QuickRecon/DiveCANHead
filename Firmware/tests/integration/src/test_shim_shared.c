/*
 * Shared memory shim — zero-copy data exchange between the Python
 * test harness and the native_sim firmware.
 *
 * A POSIX shared memory region (/dev/shm/divecan_shim) holds the
 * shim_shared_state struct.  Python writes cell values directly;
 * firmware reads them on its natural driver poll schedule.  A 1 ms
 * k_timer syncs firmware outputs (uptime, solenoids) TO shared
 * memory and pushes Python-written analog/battery values INTO the
 * Zephyr adc_emul + gpio_sim APIs.
 *
 * Digital cell PPO2 values bypass the timer entirely — the UART
 * shim's TX callback reads them straight from shared memory when
 * the firmware driver polls (see test_shim_uart.c handle_command).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>

#include "test_shim_shared.h"
#include "test_shim_adc.h"
#include "test_shim_gpio.h"

LOG_MODULE_REGISTER(test_shim_shared, LOG_LEVEL_INF);

/* Host-side adapter (compiled into native_simulator runner) */
extern void *shim_host_shm_create(const char *name, unsigned long size);
extern void  shim_host_shm_unlink(const char *name);

static struct shim_shared_state *shared;

struct shim_shared_state *shim_shared_get(void)
{
    return shared;
}

/* ---- Sync timer: firmware ↔ shared memory --------------------------- */

static float last_battery;
static uint8_t last_bus;

static void sync_loop(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (shared == NULL) {
        k_msleep(1);
    }

    while (true) {
        /* Firmware → shared memory */
        shared->uptime_us = k_ticks_to_us_floor64(k_uptime_ticks());

        int sol[4];
        shim_gpio_get_solenoids(sol);
        for (int i = 0; i < 4; ++i) {
            shared->solenoids[i] = sol[i];
        }

        /* Battery and bus go through emulator APIs (low-frequency,
         * not timing-critical).  Analog cell values are handled
         * deterministically by the adc_emul value callback registered
         * in test_shim_adc.c — no sync needed for those. */
        float bv = shared->battery_voltage;
        if (bv != last_battery) {
            last_battery = bv;
            (void)shim_adc_set_battery_voltage(bv);
        }

        uint8_t bus = shared->bus_active;
        if (bus != last_bus) {
            last_bus = bus;
            shim_gpio_set_bus_active(bus != 0);
        }

        k_msleep(1);
    }
}

K_THREAD_DEFINE(shim_sync_tid, 2048, sync_loop, NULL, NULL, NULL, 2, 0, 0);

/* ---- Init ----------------------------------------------------------- */

static int shim_shared_init(void)
{
    shared = shim_host_shm_create(SHIM_SHM_NAME,
                                   sizeof(struct shim_shared_state));
    if (shared == NULL) {
        LOG_ERR("failed to create shared memory " SHIM_SHM_NAME);
        return -ENOMEM;
    }

    /* Seed sensible defaults so firmware boots cleanly before the
     * Python harness connects. */
    for (int i = 0; i < 3; ++i) {
        shared->digital_ppo2[i] = 0.21f;
        shared->analog_millis[i] = 0.21f * 50.0f; /* 2 mV/cb convention */
    }
    shared->battery_voltage = 7.4f;
    shared->bus_active = 1;

    LOG_INF("shared memory " SHIM_SHM_NAME " mapped (%zu bytes)",
            sizeof(struct shim_shared_state));
    return 0;
}

/* Run after ADC/GPIO shim init (60) but before cell threads start */
SYS_INIT(shim_shared_init, POST_KERNEL, 61);
