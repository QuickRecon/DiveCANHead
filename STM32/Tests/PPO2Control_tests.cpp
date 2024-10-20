#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "PPO2Control.h"

// All the C stuff has to be externed
extern "C"
{
    void setSolenoidOn()
    {
        mock().actualCall("setSolenoidOn");
    }
    void setSolenoidOff()
    {
        mock().actualCall("setSolenoidOff");
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


