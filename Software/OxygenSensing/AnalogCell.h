#ifndef _ANALOGCELL_H
#define _ANALOGCELL_H

#include "ICell.h"

namespace OxygenSensing
{
    using CalCoeff_t = float;

    constexpr CalCoeff_t ANALOG_CAL_UPPER = 1000000000.0;
    constexpr CalCoeff_t ANALOG_CAL_LOWER = 0.0;
    constexpr int8_t ADC_SAMPLE_COUNT = 64; // TODO: do this ADC side, probably faster

    // Link our class to real hardware
    using AnalogPort = enum class e_AnalogPort {
        C1 = 0,
        C2 = 1,
        C3 = 2
    };

    const uint32_t adc_offset[] = {
        2200,//5594,
        2350,//8640,
        0};

    // ADC port mapping
    const ADC_MUXPOS_t analogPortMap[] = {
        ADC_MUXPOS_AIN22_gc,
        ADC_MUXPOS_AIN23_gc,
        ADC_MUXPOS_AIN24_gc};

    class AnalogCell : public ICell
    {
    public:
        explicit AnalogCell(const AnalogPort in_port);
        void sample() final;
        PPO2_t getPPO2() final;
        Millivolts_t getMillivolts() final;
        void calibrate(PPO2_t PPO2) final;

    private:
        eeprom_address_t eepromAddress(const uint8_t i) const { return (static_cast<uint8_t>(port) * sizeof(CalCoeff_t)) + i; }

        // We need a float because trying to be clever
        // and doing it all with integer arithmatic feels
        // like a great way to have subtly wrong math
        CalCoeff_t calibrationCoeff;
        AnalogPort port;
        diff_adc_result_t adcSample;
    };
}
#endif
