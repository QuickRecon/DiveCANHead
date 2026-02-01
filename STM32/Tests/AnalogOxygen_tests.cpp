/**
 * @file AnalogOxygen_tests.cpp
 * @brief Unit tests for AnalogOxygen sensor calculation functions
 *
 * Tests cover:
 * - ADC counts to millivolts conversion (Analog_CountsToMillivolts)
 * - PPO2 calculation from ADC counts (Analog_CalculatePPO2)
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include <cmath>

extern "C"
{
#include "errors.h"
#include "common.h"
#include "AnalogOxygen.h"

    /* Extern declarations for internal (non-static) calculation functions */
    Millivolts_t Analog_CountsToMillivolts(int16_t adcCounts);
    CalCoeff_t Analog_CalculatePPO2(int16_t adcCounts, CalCoeff_t calibrationCoefficient);

    /* Mock for AnalogCellSample - logs cell data, not needed for pure calculation tests */
    void AnalogCellSample(uint8_t cellNumber, PrecisionPPO2_t precisionPPO2,
                          int16_t adcCounts, Millivolts_t millivolts, CellStatus_t status)
    {
        mock().actualCall("AnalogCellSample")
            .withParameter("cellNumber", cellNumber);
    }

    /* Mock for serial_printf - used in debug output */
    void serial_printf(const char *fmt, ...)
    {
        /* Suppress debug output in tests */
    }

    /* FreeRTOS stubs */
    BaseType_t xQueueGenericSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait, const BaseType_t xCopyPosition)
    {
        return pdTRUE;
    }

    /* ADC stubs */
    int16_t GetInputValue(uint8_t inputIndex)
    {
        return 0;
    }

    uint32_t GetInputTicks(uint8_t inputIndex)
    {
        return 0;
    }

    void BlockForADC(uint8_t inputIndex)
    {
        /* Do nothing in tests */
    }

    /* Flash stubs */
    bool GetCalibration(uint8_t cellNumber, CalCoeff_t *calibrationCoefficient)
    {
        *calibrationCoefficient = 1.0f;
        return true;
    }

    bool SetCalibration(uint8_t cellNumber, CalCoeff_t calibrationCoefficient)
    {
        return true;
    }
}

/* ============================================================================
 * TEST GROUP: ADC Counts to Millivolts Conversion
 * ============================================================================
 * COUNTS_TO_MILLIS = (0.256 * 100000) / 32767 = 0.78128...
 * This converts ADC counts to micro-millivolts (mV * 100)
 * ============================================================================ */
TEST_GROUP(Analog_CountsToMillivolts)
{
    void setup() {}
    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/* Zero counts should return zero millivolts */
TEST(Analog_CountsToMillivolts, ZeroCounts)
{
    Millivolts_t mv = Analog_CountsToMillivolts(0);
    CHECK_EQUAL(0, mv);
}

/* Positive counts conversion */
TEST(Analog_CountsToMillivolts, PositiveCounts)
{
    /* 1000 counts * 0.78128 = 781.28, rounds to 781 */
    Millivolts_t mv = Analog_CountsToMillivolts(1000);
    CHECK_EQUAL(781, mv);
}

/* Negative counts should use absolute value */
TEST(Analog_CountsToMillivolts, NegativeCountsUsesAbs)
{
    Millivolts_t mvPos = Analog_CountsToMillivolts(1000);
    Millivolts_t mvNeg = Analog_CountsToMillivolts(-1000);
    CHECK_EQUAL(mvPos, mvNeg);
}

/* Max positive counts (15-bit ADC) */
TEST(Analog_CountsToMillivolts, MaxPositiveCounts)
{
    /* 32767 counts * 0.78128 = 25600 (full scale) */
    Millivolts_t mv = Analog_CountsToMillivolts(32767);
    CHECK_EQUAL(25600, mv);
}

/* Min negative counts */
TEST(Analog_CountsToMillivolts, MinNegativeCounts)
{
    /* -32768 counts -> abs = 32768, * 0.78128 = 25600.78, rounds to 25601 */
    Millivolts_t mv = Analog_CountsToMillivolts(-32768);
    CHECK_EQUAL(25601, mv);
}

/* Known conversion: ~9mV in air typical for galvanic cell */
/* 9mV = 900 in micro-mV units, so counts = 900 / 0.78128 = ~1152 counts */
TEST(Analog_CountsToMillivolts, TypicalAirReading)
{
    Millivolts_t mv = Analog_CountsToMillivolts(1152);
    /* 1152 * 0.78128 = 899.95, rounds to 900 */
    CHECK_EQUAL(900, mv);
}

/* Small count value */
TEST(Analog_CountsToMillivolts, SmallCount)
{
    /* 1 count * 0.78128 = 0.78, rounds to 1 */
    Millivolts_t mv = Analog_CountsToMillivolts(1);
    CHECK_EQUAL(1, mv);
}

/* ============================================================================
 * TEST GROUP: PPO2 Calculation
 * ============================================================================
 * PPO2 = counts * COUNTS_TO_MILLIS * calibrationCoefficient
 * For a galvanic cell in air (~9mV), with cal coeff ~0.0233, PPO2 should be ~21
 * ============================================================================ */
TEST_GROUP(Analog_CalculatePPO2)
{
    void setup() {}
    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/* Zero counts should return zero PPO2 */
TEST(Analog_CalculatePPO2, ZeroCounts)
{
    CalCoeff_t ppo2 = Analog_CalculatePPO2(0, 1.0f);
    DOUBLES_EQUAL(0.0f, ppo2, 0.001f);
}

/* Nominal air reading with typical calibration */
/* ~1152 counts (9mV) * 0.78128 * 0.0233 = ~21 (0.21 bar) */
TEST(Analog_CalculatePPO2, NominalAirReading)
{
    /* counts * COUNTS_TO_MILLIS gives micro-mV */
    /* 1152 * 0.78128 = 900 micro-mV */
    /* 900 * 0.0233 = 20.97, close to 21 */
    CalCoeff_t ppo2 = Analog_CalculatePPO2(1152, 0.0233f);
    DOUBLES_EQUAL(21.0f, ppo2, 1.0f); /* Within 1 PPO2 unit */
}

/* High O2 (1.6 PPO2) */
/* 1.6 / 0.21 * 1152 counts = ~8777 counts at same cal */
TEST(Analog_CalculatePPO2, HighO2Reading)
{
    CalCoeff_t ppo2 = Analog_CalculatePPO2(8777, 0.0233f);
    /* 8777 * 0.78128 * 0.0233 = 159.8 */
    DOUBLES_EQUAL(160.0f, ppo2, 2.0f);
}

/* Negative counts should use absolute value */
TEST(Analog_CalculatePPO2, NegativeCountsUsesAbs)
{
    CalCoeff_t ppo2Pos = Analog_CalculatePPO2(1000, 0.02f);
    CalCoeff_t ppo2Neg = Analog_CalculatePPO2(-1000, 0.02f);
    DOUBLES_EQUAL(ppo2Pos, ppo2Neg, 0.001f);
}

/* Zero calibration coefficient */
TEST(Analog_CalculatePPO2, ZeroCalibration)
{
    CalCoeff_t ppo2 = Analog_CalculatePPO2(1000, 0.0f);
    DOUBLES_EQUAL(0.0f, ppo2, 0.001f);
}

/* Calibration effect: double the coefficient doubles the PPO2 */
TEST(Analog_CalculatePPO2, CalibrationEffect)
{
    CalCoeff_t ppo2_1x = Analog_CalculatePPO2(1000, 0.02f);
    CalCoeff_t ppo2_2x = Analog_CalculatePPO2(1000, 0.04f);
    DOUBLES_EQUAL(ppo2_1x * 2.0f, ppo2_2x, 0.001f);
}

/* Boundary: max counts with small cal coeff */
TEST(Analog_CalculatePPO2, MaxCountsSmallCal)
{
    /* 32767 counts * 0.78128 * 0.001 = 25.6 */
    CalCoeff_t ppo2 = Analog_CalculatePPO2(32767, 0.001f);
    DOUBLES_EQUAL(25.6f, ppo2, 0.1f);
}

/* Verify formula matches expected: counts * 0.78128 * cal */
TEST(Analog_CalculatePPO2, FormulaVerification)
{
    /* Using exact values to verify the formula */
    int16_t counts = 10000;
    CalCoeff_t cal = 0.01f;
    /* Expected: 10000 * (0.256 * 100000 / 32767) * 0.01 = 10000 * 0.78128 * 0.01 = 78.128 */
    CalCoeff_t expected = 10000.0f * ((0.256f * 100000.0f) / 32767.0f) * 0.01f;
    CalCoeff_t ppo2 = Analog_CalculatePPO2(counts, cal);
    DOUBLES_EQUAL(expected, ppo2, 0.01f);
}

/* Typical calibration coefficient range check */
/* Valid cal coeffs are between ANALOG_CAL_LOWER (0.01428) and ANALOG_CAL_UPPER (0.02625) */
TEST(Analog_CalculatePPO2, TypicalCalRange)
{
    /* With 1152 counts (~9mV) and cal at mid-range (~0.02) */
    CalCoeff_t ppo2 = Analog_CalculatePPO2(1152, 0.02f);
    /* 1152 * 0.78128 * 0.02 = 18.0 */
    DOUBLES_EQUAL(18.0f, ppo2, 0.5f);
}

/* Very small counts */
TEST(Analog_CalculatePPO2, SmallCounts)
{
    CalCoeff_t ppo2 = Analog_CalculatePPO2(10, 0.02f);
    /* 10 * 0.78128 * 0.02 = 0.156 */
    DOUBLES_EQUAL(0.156f, ppo2, 0.01f);
}
