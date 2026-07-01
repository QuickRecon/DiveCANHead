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

#endif
