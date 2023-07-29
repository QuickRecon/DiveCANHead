#include "AnalogCell.h"
namespace OxygenSensing
{
    AnalogCell::AnalogCell(const AnalogPort in_port) : port(in_port)
    {
        // Dredge up the cal-coefficient from the eeprom
        uint8_t bytes[sizeof(CalCoeff_t)];
        for (uint8_t i = 0; i < sizeof(CalCoeff_t); ++i)
        {
            bytes[i] = EEPROM_Read(eepromAddress(i));
            printf("eeprom read: %hu:0x%x @ %d\n", i, bytes[i], eepromAddress(i));
        }

        // Memory unsafety isn't bad practice, its a way of life
        memcpy(&calibrationCoeff, bytes, sizeof(CalCoeff_t));

        // Assert that our calibration is reasonable, at this stage we only know if we're
        // (probably) calibrated or things are completely cooked
        printf("Got cal %f\n", calibrationCoeff);
        if ((calibrationCoeff > ANALOG_CAL_LOWER) &&
            (calibrationCoeff < ANALOG_CAL_UPPER))
        {
            setStatus(CellStatus_t::CELL_OK);
        }
        else
        {
            //setStatus(CellStatus_t::CELL_NEED_CAL);
        }
    }

    void AnalogCell::sample()
    {
        adcSample = 0;
        ADC_MUXPOS_t adc_port = analogPortMap[static_cast<uint8_t>(port)];
        for (uint8_t i = 0; i < ADC_SAMPLE_COUNT; ++i)
        {
            adcSample += ADC0_GetConversion(adc_port);
        }
        //printf("C1: %ld, millis: %d\n", adcSample / 100, getMillivolts());
    }

    PPO2_t AnalogCell::getPPO2()
    {
        PPO2_t PPO2 = 0;
        if((getStatus() == CellStatus_t::CELL_FAIL) || (getStatus() == CellStatus_t::CELL_NEED_CAL)){
            PPO2 = PPO2_FAIL; // Failed cell
        } else {
            PPO2 = static_cast<PPO2_t>(static_cast<CalCoeff_t>(adcSample) * calibrationCoeff);
        }
        return PPO2;
    }
    Millivolts_t AnalogCell::getMillivolts()
    {
        // ADC bullshit to get a millivolts, there is some fuckery with the cell isolator so
        // these are liable to change
        constexpr uint32_t adc_mult = 11;
        constexpr uint32_t adc_div = 29;

        return static_cast<Millivolts_t>((adcSample * adc_mult*10) / (adc_div*ADC_SAMPLE_COUNT));
    }

    // This is probably the most dirt simple calibration routine I can come up with
    // It has exactly zero compensation for rate of change, so don't be an idiot
    // and try to cal while the PPO2 is slewing so fast it could be on fast&furious
    void AnalogCell::calibrate(const PPO2_t PPO2)
    {
        // Ensure we're working with the latest sample
        sample();

        // Our coefficient is simply the float needed to make the current sample the current PPO2
        calibrationCoeff = static_cast<CalCoeff_t>(PPO2) / static_cast<CalCoeff_t>(adcSample);

        printf("Calibrated with coefficient %f\n", calibrationCoeff);
        // Write that shit to the eeprom
        uint8_t bytes[sizeof(CalCoeff_t)];
        memcpy(bytes, &calibrationCoeff, sizeof(CalCoeff_t));

        // We could probably do it more cleverly, but simple is good for now
        for (uint8_t i = 0; i < sizeof(CalCoeff_t); ++i)
        {
            EEPROM_Write(eepromAddress(i), bytes[i]);
            while (EEPROM_IsBusy())
            {
                // Wait until the eeprom is free to write the next byte
            }

            printf("eeprom write: %hu:0x%x @ %d\n", i, bytes[i], eepromAddress(i));
        }

        if(getStatus() == CellStatus_t::CELL_NEED_CAL){
            setStatus(CellStatus_t::CELL_OK);
        }
    }
}
