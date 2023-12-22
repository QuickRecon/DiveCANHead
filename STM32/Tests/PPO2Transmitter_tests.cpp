#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

TEST_GROUP(FirstTestGroup)
{
};


uint32_t HAL_GetTick()
{
    mock().actualCall("HAL_GetTick");
    return 0;
}

TEST(FirstTestGroup, FirstTest)
{
   FAIL("Fail me!");
}


TEST(FirstTestGroup, SecondTest)
{
    STRCMP_EQUAL("hello", "world");
}