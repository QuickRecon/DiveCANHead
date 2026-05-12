/**
 * @file common.h
 * @brief Project-wide numeric typedefs and bit-width constants.
 *
 * Provides domain-typed aliases for primitive types (satisfying SonarQube S813)
 * and shared shift/mask constants used across all modules.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

/* ---- Generic numeric typedefs ----
 *
 * SonarQube S813 forbids bare `float`/`double`/`int` in interface positions.
 * These typedefs mirror the STM32 base (see ../../STM32/Core/Src/common.h)
 * so that domain meaning is encoded in the name and the underlying width
 * can be swapped centrally if needed.
 */

typedef float Numeric_t;                /**< Generic float for in-module math */
typedef double PrecisionPPO2_t;         /**< Double-precision PPO2 in bar */
typedef float Percent_t;                /**< Generic percentage (0-100) */
typedef float ADCV_t;                   /**< ADC voltage as a float */

typedef int Status_t;                   /**< Zephyr-style 0=success, <0=errno */
typedef int Index_t;                    /**< Loop/array index where signed is needed */

/* ---- Common bit-width constants ---- */

static const uint32_t BYTE_WIDTH = 8U;
static const uint32_t TWO_BYTE_WIDTH = 16U;
static const uint32_t THREE_BYTE_WIDTH = 24U;
static const uint8_t  BYTE_MASK = 0xFFU;

#endif /* COMMON_H */
