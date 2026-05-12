#ifndef TEST_SHIM_UART_H_
#define TEST_SHIM_UART_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SHIM_CELL_MODE_DIVEO2 = 0,
    SHIM_CELL_MODE_O2S = 1,
} shim_cell_mode_t;

/**
 * Set the simulated PPO2 value reported by a digital cell.
 *
 * @param cell  1-indexed cell number (1..3)
 * @param ppo2  PPO2 in bar (e.g. 0.21 for surface air on 21% O2)
 * @return 0 on success, -EINVAL for invalid cell
 */
int shim_uart_set_digital_ppo2(uint8_t cell, float ppo2);

/**
 * Set the digital protocol used by a cell (DiveO2 or O2S).
 * Determines which response format the shim sends back.
 *
 * @param cell  1-indexed cell number (1..3)
 * @param mode  SHIM_CELL_MODE_DIVEO2 or SHIM_CELL_MODE_O2S
 * @return 0 on success, -EINVAL for invalid cell or mode
 */
int shim_uart_set_digital_mode(uint8_t cell, shim_cell_mode_t mode);

#endif /* TEST_SHIM_UART_H_ */
