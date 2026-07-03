#ifndef POSEIDON_ACCESSORIES_H
#define POSEIDON_ACCESSORIES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t percent;
    bool ever_received;
    bool fresh;
    uint16_t age_seconds;
} PoseidonGaugeStatus_t;

bool poseidon_gauge_voltage_byte(uint8_t *value);
void poseidon_gauge_status(PoseidonGaugeStatus_t *status);

/* Emit the documented Poseidon low-power shutdown sequence (actuators/alarm off,
 * then CMD 0x00 data=0x01 to the HUD and Battery). Call ONCE from the power
 * shutdown path the instant shutdown is committed and BEFORE VBUS drops — the
 * I2C peers lose power with VBUS. Stops the periodic accessory heartbeats first
 * so a normal data=0x00 frame cannot re-wake the peers after the shutdown frame. */
void poseidon_accessories_shutdown(void);

#endif
