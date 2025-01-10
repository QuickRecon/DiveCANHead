#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "PPO2Control.h"

// All the C stuff has to be externed
extern "C"
{
    typedef enum
    {
        TEST
    } DiveCANType_t;

    typedef struct OxygenCell_s
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
