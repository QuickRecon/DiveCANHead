#include "AnalogOxygen.h"

// static const uint8_t adc_addr[3] = {ADC1_ADDR, ADC1_ADDR, ADC2_ADDR};
// static const uint8_t adc_input_num[3] = {0, 1, 0};
static bool adc_selected_input[2] = {0, 0};
static const uint16_t adc_Addr[2] = {ADC1_ADDR, ADC2_ADDR};
static AnalogOxygenState_t cellStates[3] = {0};
static ADCStatus_t adcStatus[2] = {INIT, INIT};

extern void serial_printf(const char *fmt, ...);

// Forward decls of local funcs
void configureADC(void *arg);
void readADC(void *arg);

// FreeRTOS tasks
const osThreadAttr_t configureADC_attributes = {
    .name = "configureADC",
    .priority = (osPriority_t)osPriorityNormal,
    .stack_size = 1280};
osThreadId_t configureADCHandle;

const osThreadAttr_t readADC_attributes = {
    .name = "readADC",
    .priority = (osPriority_t)osPriorityNormal,
    .stack_size = 1280};

osThreadId_t readADCHandle;

void InitADCs()
{
    configureADCHandle = osThreadNew(configureADC, NULL, &configureADC_attributes);
    readADCHandle = osThreadNew(readADC, NULL, &readADC_attributes);
}

AnalogOxygenState_p InitCell(uint8_t cellNumber)
{
    AnalogOxygenState_p handle = &(cellStates[cellNumber]);
    handle->cellNumber = cellNumber;
    // ReadCalibration(handle);
    handle->adcCounts = 0;

    return handle;
}

// Dredge up the cal-coefficient from the eeprom
void ReadCalibration(AnalogOxygenState_p handle)
{
    EE_Status result = EE_ReadVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + handle->cellNumber, (uint32_t *)&(handle->calibrationCoefficient));

    if (result == EE_OK)
    {
        serial_printf("Got cal %f\r\n", handle->calibrationCoefficient);
        if ((handle->calibrationCoefficient > ANALOG_CAL_LOWER) &&
            (handle->calibrationCoefficient < ANALOG_CAL_UPPER))
        {
            handle->status = CELL_OK;
        }
    }
    else
    {
        serial_printf("EEPROM read fail on cell %d\r\n", handle->cellNumber);
        handle->status = CELL_NEED_CAL;
    }
}

// Calculate and write the eeprom
void Calibrate(AnalogOxygenState_p handle, const PPO2_t PPO2)
{
    // Our coefficient is simply the float needed to make the current sample the current PPO2
    handle->calibrationCoefficient = (CalCoeff_t)(PPO2) / (CalCoeff_t)(handle->adcCounts);

    serial_printf("Calibrated with coefficient %f\r\n", handle->calibrationCoefficient);

    // Convert it to raw bytes
    uint8_t bytes[sizeof(CalCoeff_t)];
    memcpy(bytes, &(handle->calibrationCoefficient), sizeof(CalCoeff_t));
    // Write that shit to the eeprom
    uint32_t byte = ((uint32_t)(bytes[3]) << 24) | ((uint32_t)(bytes[2]) << 16) | ((uint32_t)(bytes[1]) << 8) | (uint32_t)bytes[0];
    HAL_FLASH_Unlock();
    EE_Status result = EE_WriteVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + handle->cellNumber, byte);
    HAL_FLASH_Lock();
    if (result != EE_OK)
    {
        serial_printf("EEPROM write fail on cell %d\r\n", handle->cellNumber);
    }
}

PPO2_t getPPO2(const AnalogOxygenState_p handle)
{
    PPO2_t PPO2 = 0;
    if ((handle->status == CELL_FAIL) || (handle->status == CELL_NEED_CAL))
    {
        PPO2 = PPO2_FAIL; // Failed cell
    }
    else
    {
        PPO2 = (PPO2_t)((CalCoeff_t)abs(handle->adcCounts) * handle->calibrationCoefficient);
    }
    return PPO2;
}

Millivolts_t getMillivolts(const AnalogOxygenState_p handle)
{
    return (Millivolts_t)(((float)abs(handle->adcCounts)) * (0.256f / 0x7FFF));
}

////////////////////////////// ADC EVENTS
/// Happens in IRQ so gotta be quick
uint8_t conversionRegister[2] = {0, 0};
void ADC_I2C_Receive_Complete(uint8_t adcAddr, I2C_HandleTypeDef *hi2c)
{
    // We heard back from the ADC with a reading
    // 1: Lets work out who its for and then copy that into the state
    uint8_t adcIdx = adcAddr & 1;
    uint8_t input = adc_selected_input[adcIdx];
    uint8_t cellNumber = 2 * (adcIdx) + input;
    cellStates[cellNumber].adcCounts = (conversionRegister[0] <<8) | conversionRegister[1];
    
    // Mark the read
    adcStatus[adcIdx] = READ_COMPLETE;

    // Tell the task the ADC is ready for reconfigure
    osThreadFlagsSet(configureADCHandle, 0x0001U);
}

void ADC_I2C_Transmit_Complete(uint8_t adcAddr)
{
    if (adcAddr == ADC1_ADDR && adcStatus[0] == CONFIGURING)
    {
        adcStatus[0] = READ_READY;

        HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_SET);
    }
    else if (adcAddr == ADC2_ADDR && adcStatus[1] == CONFIGURING)
    {
        adcStatus[1] = READ_READY;

        HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_SET);
    }
}

void ADC_Ready_Interrupt(uint8_t adcAddr)
{
    if (adcAddr == ADC1_ADDR && adcStatus[0] == READ_READY)
    {
        adcStatus[0] = READ_PENDING;
        osThreadFlagsSet(readADCHandle, 0x0001U);
        HAL_GPIO_WritePin(LED6_GPIO_Port, LED6_Pin, GPIO_PIN_RESET);
    }
    else if (adcAddr == ADC2_ADDR && adcStatus[1] == READ_READY)
    {
        adcStatus[1] = READ_PENDING;
        osThreadFlagsSet(readADCHandle, 0x0001U);
    }
}

////////////////////////////// PRIVATE

void configureADC(void *arg)
{
    // Do the initial configuration of the ADC in a blocking mode to avoid building out a whole damn state machine
    for (int i = 0; i < ADC_COUNT; i++)
    {
        serial_printf("INIT ADC %d\r\n", i);
        if (HAL_I2C_IsDeviceReady(&hi2c1, adc_Addr[i] << 1, 5, 1000) != HAL_OK)
        {
            serial_printf("ADC %d NOT READY!\r\n", i);
        }
        uint16_t configuration = (uint16_t)((0 << 15) |    // Operational status: noop
                                            (0 << 12) |    // ADC Input: 0
                                            (0b111 << 9) | // PGA: 16x
                                            (0 << 8) |     // Mode: continuous conversion
                                            (0b100 << 5) | // Data rate: 128SPS
                                            (0 << 4) |     // Comparator mode: traditional
                                            (0 << 3) |     // Comparator polarity: active low
                                            (0 << 2) |     // Comparator latch: non-latching
                                            (0 << 0));     // Comparator queue: 1 conversion

        uint8_t configBytes[2] = {0};
        configBytes[1] = (uint8_t) configuration;
        configBytes[0] = (uint8_t) (configuration>>8);
        uint16_t lowThreshold = 0;
        uint16_t highThreshold = 0xFFFF;
        HAL_StatusTypeDef HAL_Status1 = HAL_I2C_Mem_Write(&hi2c1, adc_Addr[i] << 1, 0x01, 1, configBytes, 2, 1000); // Config
        HAL_StatusTypeDef HAL_Status2 = HAL_I2C_Mem_Write(&hi2c1, adc_Addr[i] << 1, 0x02, 1, (uint8_t *)&lowThreshold, 2, 1000);  // Low threshold
        HAL_StatusTypeDef HAL_Status3 = HAL_I2C_Mem_Write(&hi2c1, adc_Addr[i] << 1, 0x03, 1, (uint8_t *)&highThreshold, 2, 1000); // High threshold
        if (HAL_Status1 != HAL_OK || HAL_Status2 != HAL_OK || HAL_Status3 != HAL_OK)
        {
            serial_printf("ADC %d NOT INITED!\r\n", i);
        }

        // Read back the config to verify it
        //uint8_t readbackBytes[2] = {0};
        uint16_t readbackConfig = 0;
        HAL_I2C_Mem_Read(&hi2c1, adc_Addr[i] << 1, 0x01, 1, (uint8_t*)&readbackConfig, 2, 1000); // Config
        readbackConfig = (readbackConfig << 8) | (readbackConfig >> 8);
        //readbackConfig = (readbackBytes[0]) | (readbackBytes[1] << 8);
        if (readbackConfig != configuration)
        {
            serial_printf("ADC %d CONFIG MISMATCH %d:%d\r\n", i, configuration, readbackConfig);
        }
        else
        {
            serial_printf("ADC %d READBACK OK\r\n", i);
        }
    }

    while (true)
    {
        serial_printf("LOOP!\r\n");
        for (int i = 0; i < ADC_COUNT; i++)
        {
            // Reconfigure the ADC if they're either in INIT or READ_COMPLETE
            if (adcStatus[i] == READ_COMPLETE || adcStatus[i] == INIT)
            {
                adcStatus[i] = CONFIGURING;
                serial_printf("Configure ADC %d\r\n", i);
                // Prep the ADC
                uint16_t configuration = (uint16_t)((0 << 15) |                                       // Operational status: noop
                                                    (((uint8_t)adc_selected_input[i]) * 0b11 << 12) | // ADC Input: Inputs are either 0b00 (input 0) or 0b11 (input 1)
                                                    (0b111 << 9) |                                    // PGA: 16x
                                                    (0 << 8) |                                        // Mode: continuous conversion
                                                    (0b100 << 5) |                                    // Data rate: 128SPS
                                                    (0 << 4) |                                        // Comparator mode: traditional
                                                    (0 << 3) |                                        // Comparator polarity: active low
                                                    (0 << 2) |                                        // Comparator latch: non-latching
                                                    (0 << 0));                                        // Comparator queue: 1 conversion

                while (hi2c1.State != HAL_I2C_STATE_READY)
                {
                    serial_printf("Waiting on I2C port...\r\n");
                    osDelay(5);
                }

                HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_SET);

                uint8_t configBytes[2] = {0};
                configBytes[1] = (uint8_t) configuration;
                configBytes[0] = (uint8_t) (configuration>>8);
                if (HAL_I2C_Mem_Write_IT(&hi2c1, adc_Addr[i] << 1, 0x01, 1, configBytes, 2) != HAL_OK)
                {
                    serial_printf("UNABLE TO CONFIGURE ADC %d", i);
                }
            }
        }
        osThreadFlagsWait(0x0001U, osFlagsWaitAny, osWaitForever);
        // We heard back that we finished our read, time to reconfigure for the next input
        for (int i = 0; i < ADC_COUNT; i++)
        {
            if (adcStatus[i] == READ_COMPLETE)
            {
                // BEGIN DEBUG
                uint8_t adcIdx = adc_Addr[i] & 1;
                uint8_t input = adc_selected_input[adcIdx];
                uint8_t cellNumber = 2 * (adcIdx) + input;
                serial_printf("Read complete ADC %d input: %d Value: %d\r\n", i, adc_selected_input[i], cellStates[cellNumber].adcCounts);

                // END DEBUG
                adc_selected_input[i] = !adc_selected_input[i]; // Chop over to the other input as we go around again
            }
        }
    }
}

void readADC(void *arg)
{
    while (true)
    {
        osThreadFlagsWait(0x0001U, osFlagsWaitAny, osWaitForever);
        // We heard back that we finished our read, time to reconfigure for the next input

        for (int i = 0; i < ADC_COUNT; i++)
        {
            if (adcStatus[i] == READ_PENDING)
            {
                serial_printf("Read ADC %d\r\n", i);
                while (hi2c1.State != HAL_I2C_STATE_READY)
                {
                    serial_printf("Waiting on I2C port...\r\n");
                    osDelay(5);
                }
                HAL_I2C_Mem_Read_IT(&hi2c1, adc_Addr[i] << 1, 0x00, 1, conversionRegister, 2); // TODO, convert this to a non-blocking call
            }
        }
    }
}
