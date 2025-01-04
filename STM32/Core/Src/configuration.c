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

bool CellValid(Configuration_t config, uint8_t cellNumber)
{
    /* Check that the enum */
    uint8_t cellVal = (getConfigBytes(&config) >> (8u + (cellNumber * 2))) & 0b11u;
    return (cellVal == (uint8_t)CELL_ANALOG) || (cellVal == (uint8_t)CELL_DIGITAL);
}

bool ConfigurationValid(Configuration_t config)
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

    /* Check for incompatible states */
    valid = valid && ((!config.enableUartPrinting) || (config.cell2 == CELL_ANALOG)); /* If uart printing is on, cell2 MUST be analog */

    /* Can't have digital cal if no digital cells */
    const bool configDigitalCellValid = !((config.calibrationMode == CAL_DIGITAL_REFERENCE) && (config.cell1 == CELL_ANALOG) && (config.cell2 == CELL_ANALOG) && (config.cell3 == CELL_ANALOG));
    valid = valid && configDigitalCellValid;

    return valid;
}

Configuration_t loadConfiguration(void)
{
    Configuration_t config = {0};
    if (GetConfiguration(&config) && ConfigurationValid(config))
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

bool saveConfiguration(const Configuration_t *const config)
{
    bool valid = ConfigurationValid(*config);
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
