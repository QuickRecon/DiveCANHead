#include "ext_adc.h"
#include "main.h"
#include "cmsis_os.h"
#include "../errors.h"
#include <stdbool.h>

const uint8_t ADC1_ADDR = 0x48;
const uint8_t ADC2_ADDR = 0x49;

static const uint8_t ADC_INPUT_1 = 0b00;
static const uint8_t ADC_INPUT_2 = 0b11;

static const uint8_t ADC_CONFIG_REGISTER = 0x01;
static const uint8_t ADC_LOW_THRESHOLD_REGISTER = 0x02;
static const uint8_t ADC_HIGH_THRESHOLD_REGISTER = 0x03;

static const uint8_t TRANSMIT_COMPLETE_FLAG = 0b01;
static const uint8_t READ_READY_FLAG = 0b010;
static const uint8_t READ_COMPLETE_FLAG = 0b100;

const uint32_t I2C_TIMEOUT = pdMS_TO_TICKS(1000);

extern I2C_HandleTypeDef hi2c1;

#define ADC_COUNT 4
static QueueHandle_t QInputValues[ADC_COUNT] = {NULL, NULL, NULL, NULL};
static QueueHandle_t QInputTicks[ADC_COUNT] = {NULL, NULL, NULL, NULL};

extern void serial_printf(const char *fmt, ...);

// Forward decls of local funcs
// /void configureADC(void *arg);
void ADCTask(void *arg);

#define ADCTASK_STACK_SIZE 450 // 400 by static analysis

// FreeRTOS tasks
static uint32_t ADCTask_buffer[ADCTASK_STACK_SIZE];
static StaticTask_t ADCTask_ControlBlock;
const osThreadAttr_t ADCTask_attributes = {
    .name = "ADCTask",
    .cb_mem = &ADCTask_ControlBlock,
    .cb_size = sizeof(ADCTask_ControlBlock),
    .stack_mem = &ADCTask_buffer[0],
    .stack_size = sizeof(ADCTask_buffer),
    .priority = (osPriority_t)ADC_PRIORITY};
osThreadId_t ADCTaskHandle;

void InitADCs(void)
{
    ADCTaskHandle = osThreadNew(ADCTask, NULL, &ADCTask_attributes);
    // readADCHandle = osThreadNew(readADC, NULL, &readADC_attributes);
}

uint32_t GetInputTicks(uint8_t inputIndex)
{
    uint32_t ticks = 0;
    if (QInputTicks[inputIndex] != NULL)
    {
        if (pdFALSE == xQueuePeek(QInputTicks[inputIndex], &ticks, 0))
        {
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
    }
    return ticks;
}
uint16_t GetInputValue(uint8_t inputIndex)
{
    uint16_t adcCounts = 0;
    if (QInputValues[inputIndex] != NULL)
    {
        if (pdFALSE == xQueuePeek(QInputValues[inputIndex], &adcCounts, 0))
        {
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
    }
    return adcCounts;
}

void BlockForADC(uint8_t inputIndex)
{
    bool valuesReset = xQueueReset(QInputValues[inputIndex]);
    bool inputReset = xQueueReset(QInputValues[inputIndex]);

    if (valuesReset && inputReset) // reset always returns pdPASS, so this should always evaluate to true
    {
        uint32_t ticks = 0;
        uint16_t adcCounts = 0;
        bool ticksAvailable = xQueuePeek(QInputTicks[inputIndex], &ticks, pdMS_TO_TICKS(1000));
        bool inputAvailable = xQueuePeek(QInputValues[inputIndex], &adcCounts, pdMS_TO_TICKS(1000));

        if ((!ticksAvailable) && (!inputAvailable))
        {
            // Data is not avaliable, but the later code is able to handle that,
            // This method mainly exists to rest for a convenient, event-based amount
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
    }
    else
    {
        NON_FATAL_ERROR(UNREACHABLE_ERROR);
    }
}

////////////////////////////// ADC EVENTS
/// Happens in IRQ so gotta be quick
void ADC_I2C_Receive_Complete(uint8_t adcAddr, I2C_HandleTypeDef *hi2c)
{
    uint32_t err = osThreadFlagsSet(ADCTaskHandle, READ_COMPLETE_FLAG);
    if ((err & FLAG_ERR_MASK) == FLAG_ERR_MASK)
    { // Detect any flag error states
        NON_FATAL_ERROR_ISR_DETAIL(FLAG_ERROR, err);
    }
}

void ADC_I2C_Transmit_Complete(uint8_t adcAddr)
{
    uint32_t err = osThreadFlagsSet(ADCTaskHandle, TRANSMIT_COMPLETE_FLAG);
    if ((err & FLAG_ERR_MASK) == FLAG_ERR_MASK)
    { // Detect any flag error states
        NON_FATAL_ERROR_ISR_DETAIL(FLAG_ERROR, err);
    }
}

void ADC_Ready_Interrupt(uint8_t adcAddr)
{
    uint32_t err = osThreadFlagsSet(ADCTaskHandle, READ_READY_FLAG);
    if ((err & FLAG_ERR_MASK) == FLAG_ERR_MASK)
    { // Detect any flag error states
        NON_FATAL_ERROR_ISR_DETAIL(FLAG_ERROR, err);
    }
}

void configureADC(uint16_t configuration, InputState_s input)
{
    uint16_t lowThreshold = 0;
    uint16_t highThreshold = 0xFFFF;

    uint8_t configBytes[2] = {0};
    configBytes[1] = (uint8_t)configuration;
    configBytes[0] = (uint8_t)(configuration >> 8);

    if (HAL_OK != HAL_I2C_Mem_Write_IT(&hi2c1, (uint16_t)((uint16_t)(input.adcAddress) << 1), ADC_LOW_THRESHOLD_REGISTER, sizeof(ADC_LOW_THRESHOLD_REGISTER), (uint8_t *)&lowThreshold, sizeof(lowThreshold)))
    {
        NON_FATAL_ERROR(I2C_BUS_ERROR);
    }
    if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(TRANSMIT_COMPLETE_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
    {
        NON_FATAL_ERROR(FLAG_ERROR);
    }

    if (HAL_OK != HAL_I2C_Mem_Write_IT(&hi2c1, (uint16_t)((uint16_t)(input.adcAddress) << 1), ADC_HIGH_THRESHOLD_REGISTER, sizeof(ADC_HIGH_THRESHOLD_REGISTER), (uint8_t *)&highThreshold, sizeof(highThreshold)))
    {
        NON_FATAL_ERROR(I2C_BUS_ERROR);
    }
    if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(TRANSMIT_COMPLETE_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
    {
        NON_FATAL_ERROR(FLAG_ERROR);
    }

    if (HAL_OK != HAL_I2C_Mem_Write_IT(&hi2c1, (uint16_t)((uint16_t)(input.adcAddress) << 1), ADC_CONFIG_REGISTER, sizeof(ADC_CONFIG_REGISTER), configBytes, sizeof(configBytes)))
    {
        NON_FATAL_ERROR(I2C_BUS_ERROR);
    }
    if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(TRANSMIT_COMPLETE_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
    {
        NON_FATAL_ERROR(FLAG_ERROR);
    }
}

// Tasks
static InputState_s adcInput[ADC_COUNT] = {0};

void ADCTask(void *arg)
{
    const uint8_t INPUT_1 = 0;
    const uint8_t INPUT_2 = 1;
    const uint8_t INPUT_3 = 2;
    const uint8_t INPUT_4 = 3;

    adcInput[INPUT_1].adcAddress = ADC1_ADDR;
    adcInput[INPUT_1].inputIndex = ADC_INPUT_1;

    adcInput[INPUT_2].adcAddress = ADC1_ADDR;
    adcInput[INPUT_2].inputIndex = ADC_INPUT_2;

    adcInput[INPUT_3].adcAddress = ADC2_ADDR;
    adcInput[INPUT_3].inputIndex = ADC_INPUT_1;

    adcInput[INPUT_4].adcAddress = ADC2_ADDR;
    adcInput[INPUT_4].inputIndex = ADC_INPUT_2;

    for (uint8_t i = 0; i < ADC_COUNT; ++i)
    {
        adcInput[i].QInputValue = xQueueCreateStatic(1, sizeof(uint16_t), adcInput[i].QInputValue_Storage, &(adcInput[i].QInputValue_QueueStruct));
        adcInput[i].QInputTick = xQueueCreateStatic(1, sizeof(uint32_t), adcInput[i].QInputTicks_Storage, &(adcInput[i].QInputTicks_QueueStruct));

        QInputValues[i] = adcInput[i].QInputValue;
        QInputTicks[i] = adcInput[i].QInputTick;
    }

    while (1 != 0) // Loop forever as we are an RTOS task
    {
        for (uint8_t i = 0; i < ADC_COUNT; ++i)
        {
            // Configure the ADC
            uint16_t configuration = (uint16_t)((0 << 15) |                      // Operational status: noop
                                                (adcInput[i].inputIndex << 12) | // ADC Input: Inputs are either 0b00 (input 0) or 0b11 (input 1)
                                                (0b111 << 9) |                   // PGA: 16x
                                                (1 << 8) |                       // Mode: single shot
                                                (0b00 << 5) |                    // Data rate: 8SPS
                                                (0 << 4) |                       // Comparator mode: traditional
                                                (0 << 3) |                       // Comparator polarity: active low
                                                (0 << 2) |                       // Comparator latch: non-latching
                                                0);                              // Comparator queue: 1 conversion
            // Configure for the next input
            configureADC(configuration, adcInput[i]); // Will yield during I2C TX

            // Start the conversion
            uint16_t triggerReadConfiguration = (uint16_t)((1 << 15) | configuration); // Set the operation bit to start a conversion
            uint8_t configBytes[2] = {0};
            configBytes[1] = (uint8_t)triggerReadConfiguration;
            configBytes[0] = (uint8_t)(triggerReadConfiguration >> 8);
            if (HAL_OK != HAL_I2C_Mem_Write_IT(&hi2c1, (uint16_t)((uint16_t)(adcInput[i].adcAddress) << 1), ADC_CONFIG_REGISTER, sizeof(ADC_CONFIG_REGISTER), configBytes, sizeof(configBytes)))
            {
                NON_FATAL_ERROR(I2C_BUS_ERROR);
            }

            if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(READ_READY_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
            {
                NON_FATAL_ERROR(FLAG_ERROR);
            }

            // Read the ADC
            uint8_t conversionRegister[2] = {0}; // Memory space to recieve inputs into
            if (HAL_OK != HAL_I2C_Mem_Read_IT(&hi2c1, (uint16_t)((uint16_t)(adcInput[i].adcAddress) << 1), 0x00, 1, conversionRegister, sizeof(conversionRegister)))
            {
                NON_FATAL_ERROR(I2C_BUS_ERROR);
            }

            if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(READ_COMPLETE_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
            {
                NON_FATAL_ERROR(FLAG_ERROR);
            }

            // Export the value to the queue
            uint16_t adcCounts = (uint16_t)((uint16_t)conversionRegister[0] << 8) | conversionRegister[1];
            uint32_t ticks = HAL_GetTick();
            bool valueWrite = xQueueOverwrite(adcInput[i].QInputValue, &adcCounts);
            if (valueWrite) // Make sure our value got updated first, we don't want the ticks queue to lie about the currency of the data
            {
                bool tickWrite = xQueueOverwrite(adcInput[i].QInputTick, &ticks);
                if(!tickWrite){
                    NON_FATAL_ERROR(QUEUEING_ERROR);
                }
            } else {
                NON_FATAL_ERROR(QUEUEING_ERROR);
            }
        }
    }
}
