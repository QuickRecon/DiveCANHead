#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include <math.h>

#include "PPO2Control.h"

// All the C stuff has to be externed
extern "C"
{
    typedef enum
    {
        TEST
    } DiveCANType_t;

    typedef struct OxygenCellStruct
    {
        int test;
    } OxygenCell_t;

    void setSolenoidOn()
    {
        mock().actualCall("setSolenoidOn");
    }
    void setSolenoidOff()
    {
        mock().actualCall("setSolenoidOff");
    }
    void txPIDState(const DiveCANType_t deviceType, PIDNumeric_t proportional_gain, PIDNumeric_t integral_gain, PIDNumeric_t derivative_gain, PIDNumeric_t integral_state, PIDNumeric_t derivative_state, PIDNumeric_t duty_cycle, PIDNumeric_t precisionConsensus)
    {
        mock().actualCall("txPIDState");
    }
    void txPrecisionCells(const DiveCANType_t deviceType, OxygenCell_t c1, OxygenCell_t c2, OxygenCell_t c3)
    {
        mock().actualCall("txPrecisionCells");
    }

    void LogPIDState(const PIDState_t *const pid_state, PIDNumeric_t dutyCycle, PIDNumeric_t setpoint)
    {
        mock().actualCall("LogPIDState");
    }
}

TEST_GROUP(PPO2Control){
    void setup(){

    }

    void teardown(){
        mock().removeAllComparatorsAndCopiers();
mock().clear();
}
}
;

TEST(PPO2Control, SetpointGlobalStateTracks)
{
    for (uint8_t i = 0; i < 255; i++)
    {
        setSetpoint(i);
        CHECK(getSetpoint() == i);
    }
}

TEST(PPO2Control, AtmosGlobalStateTracks)
{
    for (uint16_t i = 0; i < 25000; i++)
    {
        setAtmoPressure(i);
        CHECK(getAtmoPressure() == i);
    }
}

// Test PID control loop behavior
TEST(PPO2Control, ProportionalTermOnly)
{
    PIDState_t state = {0};
    state.proportionalGain = 2.0f;
    
    // With setpoint = 1.0 and measurement = 0.5, error = 0.5
    // P term should be 2.0 * 0.5 = 1.0
    PIDNumeric_t result = updatePID(1.0f, 0.5f, &state);
    DOUBLES_EQUAL(1.0f, result, 0.0001f);
    
    // With setpoint = 1.0 and measurement = 0.75, error = 0.25
    // P term should be 2.0 * 0.25 = 0.5
    result = updatePID(1.0f, 0.75f, &state);
    DOUBLES_EQUAL(0.5f, result, 0.0001f);
}

TEST(PPO2Control, IntegralTermAccumulates)
{
    PIDState_t state = {0};
    state.integralGain = 0.1f;
    state.integralMax = 1.0f;
    state.integralMin = 0.0f;
    
    // With setpoint = 1.0 and measurement = 0.5, error = 0.5
    // I term first iteration should be 0.1 * 0.5 = 0.05
    PIDNumeric_t result = updatePID(1.0f, 0.5f, &state);
    DOUBLES_EQUAL(0.05f, result, 0.0001f);
    
    // Second iteration should accumulate
    // Previous I = 0.05, new error = 0.5
    // New I = 0.05 + (0.1 * 0.5) = 0.1
    result = updatePID(1.0f, 0.5f, &state);
    DOUBLES_EQUAL(0.1f, result, 0.0001f);
}

TEST(PPO2Control, IntegralWindupProtection)
{
    PIDState_t state = {0};
    state.integralGain = 1.0f;
    state.integralMax = 0.5f;
    state.integralMin = 0.0f;
    
    // With high gain and persistent error, integral should hit max
    for(int i = 0; i < 10; i++) {
        updatePID(1.0f, 0.0f, &state);
    }
    
    DOUBLES_EQUAL(0.5f, state.integralState, 0.0001f);
    CHECK(state.saturationCount > 0);
    
    // When error goes negative, integral should reset
    updatePID(0.0f, 1.0f, &state);
    DOUBLES_EQUAL(0.0f, state.integralState, 0.0001f);
    CHECK_EQUAL(0, state.saturationCount);
}

TEST(PPO2Control, DerivativeTermRespondsToChange)
{
    PIDState_t state = {0};
    state.derivativeGain = 1.0f;
    state.derivativeState = 0.5f; // Previous measurement
    
    // New measurement is 0.7, derivative should be (0.5 - 0.7) = -0.2
    PIDNumeric_t result = updatePID(1.0f, 0.7f, &state);
    DOUBLES_EQUAL(-0.2f, result, 0.0001f);
    
    // Verify derivative state was updated
    DOUBLES_EQUAL(0.7f, state.derivativeState, 0.0001f);
}

TEST(PPO2Control, AllTermsCombine)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0f;
    state.integralGain = 0.1f;
    state.derivativeGain = 0.5f;
    state.derivativeState = 0.5f;
    state.integralMax = 1.0f;
    state.integralMin = 0.0f;
    
    // With setpoint = 1.0 and measurement = 0.5:
    // P term = 1.0 * 0.5 = 0.5
    // I term = 0.1 * 0.5 = 0.05
    // D term = 0.5 * (0.5 - 0.5) = 0
    PIDNumeric_t result = updatePID(1.0f, 0.5f, &state);
    DOUBLES_EQUAL(0.55f, result, 0.0001f);
}

TEST(PPO2Control, ZeroGainsGiveZeroOutput)
{
    PIDState_t state = {0};
    // With all gains at 0, any error should result in 0 output
    PIDNumeric_t result = updatePID(1.0f, 0.0f, &state);
    DOUBLES_EQUAL(0.0f, result, 0.0001f);
}

TEST(PPO2Control, NegativeErrorProducesNegativeOutput)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0f;
    
    // With measurement > setpoint, error is negative
    // Should produce negative output to reduce oxygen
    PIDNumeric_t result = updatePID(0.7f, 1.0f, &state);
    CHECK(result < 0.0f);
}

TEST(PPO2Control, SaturationCountTracksIntegralLimits)
{
    PIDState_t state = {0};
    state.integralGain = 1.0f;
    state.integralMax = 0.5f;
    state.integralMin = -0.5f;
    
    // Push integral positive until saturated
    for(int i = 0; i < 10; i++) {
        updatePID(1.0f, 0.0f, &state);
    }
    CHECK(state.saturationCount > 0);
    
    // Reset saturation count when we come out of saturation
    updatePID(1.0, 1.1f, &state);
    CHECK_EQUAL(0, state.saturationCount);
    
    // integral state should be significantly reduced
    CHECK(fabs(state.integralState) < state.integralMax * 0.5f);
    
    /* We don't check negative saturation because we can't push the PPO2 down*/
}

TEST(PPO2Control, StepResponseBehavior)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0f;
    state.integralGain = 0.1f;
    state.derivativeGain = 0.2f;
    state.integralMax = 1.0f;
    state.integralMin = -1.0f;
    state.derivativeState = 0.0f;
    
    // Initialize at steady state
    updatePID(1.0f, 1.0f, &state);
    
    // Apply step change in measurement
    PIDNumeric_t result = updatePID(1.0f, 0.5f, &state);
    
    // First response should show:
    // P = 1.0 * 0.5 = 0.5
    // I ≈ 0.1 * 0.5 = 0.05
    // D = 0.2 * (1.0 - 0.5) = 0.1
    // Total ≈ 0.65
    DOUBLES_EQUAL(0.65f, result, 0.0001f);
    
    // Response should decrease as derivative term reduces
    result = updatePID(1.0f, 0.5f, &state);
    CHECK(result < 0.65f);
}

TEST(PPO2Control, SetpointChangeResponse)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0f;
    state.integralGain = 0.1f;
    state.derivativeGain = 0.2f;
    state.integralMax = 1.0f;
    state.integralMin = -1.0f;
    
    // Start at steady state
    updatePID(1.0f, 1.0f, &state);
    
    // Apply setpoint change
    PIDNumeric_t result = updatePID(1.5f, 1.0f, &state);
    
    // Should see positive control action
    CHECK(result > 0.0f);
    
    // Integral term should start accumulating
    PIDNumeric_t first_result = result;
    result = updatePID(1.5f, 1.0f, &state);
    CHECK(result > first_result);
}

TEST(PPO2Control, BoundaryConditions)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0f;
    state.integralGain = 0.1f;
    state.derivativeGain = 0.2f;
    state.integralMax = 1.0f;
    state.integralMin = -1.0f;
    
    // Test with zero setpoint
    PIDNumeric_t result = updatePID(0.0f, 0.1f, &state);
    CHECK(result < 0.0f);
    
    // Test with very large setpoint
    state.integralState = 0.0f; // Reset integral state
    state.derivativeState = 0.0f; // Reset derivative state
    result = updatePID(10.0f, 0.1f, &state);
    CHECK(result > 0.0f);
    
    // Test with equal setpoint and measurement at zero
    state.integralState = 0.0f; // Reset integral state
    state.derivativeState = 0.0f; // Reset derivative state
    result = updatePID(0.0f, 0.0f, &state);
    DOUBLES_EQUAL(0.0f, result, 0.0001f);
    
    // Test with very small error
    state.integralState = 0.0f; // Reset integral state
    state.derivativeState = 0.9999f; // Reset derivative state
    result = updatePID(1.0f, 0.9999f, &state);
    CHECK(fabsf(result) < 0.001f);
}
