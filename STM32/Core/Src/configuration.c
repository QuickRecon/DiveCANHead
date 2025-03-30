#include "configuration.h"
#include <string.h>
#include "Hardware/flash.h"
#include "common.h"
#include "assert.h"

uint32_t getConfigBytes(const Configuration_t *const config)
{
    (void)assert(sizeof(Configuration_t) <= sizeof(uint32_t));
    uint32_t configBits = 0;
    (void)memcpy(&configBits, config, sizeof(uint32_t));
    return configBits;
}

Configuration_t setConfigBytes(uint32_t configBits)
{
    (void)assert(sizeof(Configuration_t) <= sizeof(uint32_t));
    Configuration_t config = {0};
    (void)memcpy(&config, &configBits, sizeof(uint32_t));
    return config;
}

bool HWValid(Configuration_t config, HW_Version_t hw_version){
    bool valid = true;
    if (HW_JR == hw_version)
    {
        /* Jr only supports analog cells */
        valid = valid && (CELL_ANALOG == config.cell1);
        valid = valid && (CELL_ANALOG == config.cell2);
        valid = valid && (CELL_ANALOG == config.cell3);

        /* Only runs in battery mode */
        valid = valid && (MODE_BATTERY == config.powerMode);

        /* Only runs with 9v battery*/
        valid = valid && (V_THRESHOLD_9V == config.dischargeThresholdMode);
    }
    return valid;
}

bool CellValid(Configuration_t config, uint8_t cellNumber)
{
    /* Check that the enum */
    uint8_t cellVal = (getConfigBytes(&config) >> (8u + (cellNumber * 2))) & 0b11u;
    return (cellVal == (uint8_t)CELL_ANALOG) ||
           (cellVal == (uint8_t)CELL_DIVEO2) ||
           (cellVal == (uint8_t)CELL_O2S);
}

/**
 * @brief Validate that the configuration is valid, self consistent, and suitable for the running hardware
 * @param config configuration structure to validate
 * @param hw_version currently running hardware version
 * @return true if configuration is valid and suitable for current hardware, otherwise false
 */
bool ConfigurationValid(Configuration_t config, HW_Version_t hw_version)
{
    bool valid = true;

    /* Casting from an integer type to enums results in undefined values if outside the enum ranges
     * so we first need to do bit manipulation to ensure the provided value lands in the expected range
     */

    /* Check cells are valid */
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        valid = valid && CellValid(config, i);
    }

    uint32_t configBits = getConfigBytes(&config);

    /* Check power mode*/
    uint8_t powerMode = (configBits >> 14) & 0b11u;
    valid = valid && ((powerMode == MODE_BATTERY) ||
                      (powerMode == MODE_BATTERY_THEN_CAN) ||
                      (powerMode == MODE_CAN) ||
                      (powerMode == MODE_OFF));

    /* Check cal mode */
    uint8_t calMode = (configBits >> 16) & 0b111u;
    valid = valid && ((calMode == CAL_DIGITAL_REFERENCE) ||
                      (calMode == CAL_ANALOG_ABSOLUTE) ||
                      (calMode == CAL_TOTAL_ABSOLUTE));

    /* Check that the firmware version matches*/
    uint8_t firmwareVersion = configBits & 0xFFu;
    valid = valid && (FIRMWARE_VERSION == firmwareVersion);

    /* We've checked our enums, using fields is allowed*/

    /* Can't have digital cal if no digital cells */
    const bool configDigitalCellValid = !((config.calibrationMode == CAL_DIGITAL_REFERENCE) && (config.cell1 == CELL_ANALOG) && (config.cell2 == CELL_ANALOG) && (config.cell3 == CELL_ANALOG));
    valid = valid && configDigitalCellValid;

    valid = valid && HWValid(config, hw_version);

    return valid;
}

/**
 * @brief Load the configuration from flash, validating it is correct for the hardware
 * @param hw_version currently running hardware (to validate compatibility)
 * @return Valid configuration struct loaded from flash
 */
Configuration_t loadConfiguration(HW_Version_t hw_version)
{
    Configuration_t config = {0};
    if (GetConfiguration(&config) && ConfigurationValid(config, hw_version))
    {
        /* Everything is fine */
    }
    else
    {
        NON_FATAL_ERROR_ISR(CONFIG_ERROR); /* We need to use the isr call because that is blocking */
        config = DEFAULT_CONFIGURATION;
    }
    return config;
}

/**
 * @brief Validate and save configuration to flash
 * @param config configuration struct to save
 * @param hw_version currently running hardware (to validate compatibility)
 * @return true if successful otherwise false
 */
bool saveConfiguration(const Configuration_t *const config, HW_Version_t hw_version)
{
    bool valid = ConfigurationValid(*config, hw_version);
    if (valid)
    {
        valid = SetConfiguration(config);

        Configuration_t readbackConfig = {0};
        bool readbackOk = GetConfiguration(&readbackConfig);

        valid = (readbackOk && valid) && (getConfigBytes(&readbackConfig) == getConfigBytes(config));

        /* Clear the calibration on config change */
        valid = valid && SetCalibration(CELL_1, 0);
        valid = valid && SetCalibration(CELL_2, 0);
        valid = valid && SetCalibration(CELL_3, 0);
    }
    return valid;
}
