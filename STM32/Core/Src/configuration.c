#include "common.h"
#include "configuration.h"

const Configuration_t DefaultConfiguration = {.fields = {
                                                  .firmwareVersion = FIRMWARE_VERSION,
                                                  .cell1 = CELL_DIGITAL,
                                                  .cell2 = CELL_ANALOG,
                                                  .cell3 = CELL_ANALOG,
                                                  .powerMode = MODE_BATTERY_THEN_CAN,
                                                  .calibrationMode = CAL_DIGITAL_REFERENCE,
                                                  .enableUartPrinting = true}};

bool CellValid(Configuration_t config, uint8_t cellNumber)
{
    /* Check that the enum */
    uint8_t cellVal = (config.bits >> (8u + (cellNumber * 2))) & 0b11u;
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

    /* Check power mode*/
    uint8_t powerMode = (config.bits >> 14) & 0b11u;
    valid = valid && ((powerMode == MODE_BATTERY) ||
                      (powerMode == MODE_BATTERY_THEN_CAN) ||
                      (powerMode == MODE_CAN) ||
                      (powerMode == MODE_OFF));

    /* Check cal mode */
    uint8_t calMode = (config.bits >> 16) & 0b111u;
    valid = valid && ((calMode == CAL_DIGITAL_REFERENCE) ||
                      (calMode == CAL_ANALOG_ABSOLUTE) ||
                      (calMode == CAL_TOTAL_ABSOLUTE));

    /* Check that the firmware version matches*/
    uint8_t firmwareVersion = (config.bits) & 0xFFu;
    valid = valid && (FIRMWARE_VERSION == firmwareVersion);

    /* We've checked our enums, using fields is allowed*/

    /* Check for incompatible states */
    valid = valid && ((!config.fields.enableUartPrinting) || (config.fields.cell2 == CELL_ANALOG)); /* If uart printing is on, cell2 MUST be analog */

    return valid;
}

Configuration_t loadConfiguration(void)
{
    return DefaultConfiguration;
}

void saveConfiguration(Configuration_t config)
{
}
