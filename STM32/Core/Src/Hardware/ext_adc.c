#include "ext_adc.h"
#include "main.h"
#include "cmsis_os.h"
#include "../errors.h"
#include <stdbool.h>
#include "../Hardware/printer.h"

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

static QueueHandle_t *getInputQueue(uint8_t inputNumber)
{
    static QueueHandle_t QInputValues[ADC_COUNT] = {NULL, NULL, NULL, NULL};
    QueueHandle_t *queueHandle = NULL;
    if (inputNumber >= ADC_COUNT)
    {
        NON_FATAL_ERROR(INVALID_ADC_NUMBER);
        queueHandle = &(QInputValues[0]); /* A safe fallback */
    }
    else
    {
        queueHandle = &(QInputValues[inputNumber]);
    }
    return queueHandle;
}

static QueueHandle_t *getTicksQueue(uint8_t inputNumber)
{
    static QueueHandle_t QInputTicks[ADC_COUNT] = {NULL, NULL, NULL, NULL};
    QueueHandle_t *queueHandle = NULL;
    if (inputNumber >= ADC_COUNT)
    {
        NON_FATAL_ERROR(INVALID_ADC_NUMBER);
        queueHandle = &(QInputTicks[0]); /* A safe fallback */
    }
    else
    {
        queueHandle = &(QInputTicks[inputNumber]);
    }
    return queueHandle;
}

/* Forward decls of local funcs */
void ADCTask(void *arg);

/* FreeRTOS tasks */
static osThreadId_t *getOSThreadId(void)
{
    static osThreadId_t ADCTaskHandle;
    return &ADCTaskHandle;
}

void InitADCs(void)
{
    static uint32_t ADCTask_buffer[ADCTASK_STACK_SIZE];
    static StaticTask_t ADCTask_ControlBlock;
    static const osThreadAttr_t ADCTask_attributes = {
        .name = "ADCTask",
        .attr_bits = osThreadDetached,
        .cb_mem = &ADCTask_ControlBlock,
        .cb_size = sizeof(ADCTask_ControlBlock),
        .stack_mem = &ADCTask_buffer[0],
        .stack_size = sizeof(ADCTask_buffer),
        .priority = ADC_PRIORITY,
        .tz_module = 0,
        .reserved = 0};

    osThreadId_t *ADCTaskHandle = getOSThreadId();
    *ADCTaskHandle = osThreadNew(ADCTask, NULL, &ADCTask_attributes);
}

void DeInitADCs(void)
{
    osThreadTerminate(*getOSThreadId());
}

uint32_t GetInputTicks(uint8_t inputIndex)
{
    uint32_t ticks = 0;

    QueueHandle_t *ticksQueue = getTicksQueue(inputIndex);
    if ((*ticksQueue != NULL) && (pdTRUE == xQueuePeek(*ticksQueue, &ticks, 0)))
    {
        /* Everything is fine */
    }
    else
    {
        NON_FATAL_ERROR(TIMEOUT_ERROR);
    }
    return ticks;
}
int16_t GetInputValue(uint8_t inputIndex)
{
    int16_t adcCounts = 0;
    QueueHandle_t *inputQueue = getInputQueue(inputIndex);
    if ((*inputQueue != NULL) && (pdTRUE == xQueuePeek(*inputQueue, &adcCounts, 0)))
    {
        /* Everything is fine */
    }
    else
    {
        NON_FATAL_ERROR(TIMEOUT_ERROR);
    }
    return adcCounts;
}

void BlockForADC(uint8_t inputIndex)
{
    QueueHandle_t *ticksQueue = getTicksQueue(inputIndex);
    QueueHandle_t *inputQueue = getInputQueue(inputIndex);
    bool ticksReset = xQueueReset(*ticksQueue);
    bool inputReset = xQueueReset(*inputQueue);

    if (ticksReset && inputReset) /* reset always returns pdPASS, so this should always evaluate to true */
    {
        uint32_t ticks = 0;
        uint16_t adcCounts = 0;
        bool ticksAvailable = xQueuePeek(*ticksQueue, &ticks, pdMS_TO_TICKS(1000));
        bool inputAvailable = xQueuePeek(*inputQueue, &adcCounts, pdMS_TO_TICKS(1000));

        if ((!ticksAvailable) && (!inputAvailable))
        {
            /* Data is not available, but the later code is able to handle that,
             * This method mainly exists to rest for a convenient, event-based amount
             */
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
    }
    else
    {
        NON_FATAL_ERROR(UNREACHABLE_ERROR);
    }
}

/* ADC EVENTS -------------- */

/**
 * @brief Called from the hal when the ADC has finished its receive
 * @note Happens in IRQ so gotta be quick
 */
void ADC_I2C_Receive_Complete(void)
{
    osThreadId_t *ADCTaskHandle = getOSThreadId();
    uint32_t err = osThreadFlagsSet(*ADCTaskHandle, READ_COMPLETE_FLAG);
    /*Detect any flag error states*/
    if ((err & FLAG_ERR_MASK) == FLAG_ERR_MASK)
    {
        NON_FATAL_ERROR_ISR_DETAIL(FLAG_ERROR, err);
    }
}

void ADC_I2C_Transmit_Complete(void)
{
    osThreadId_t *ADCTaskHandle = getOSThreadId();
    uint32_t err = osThreadFlagsSet(*ADCTaskHandle, TRANSMIT_COMPLETE_FLAG);
    /*Detect any flag error states*/
    if ((err & FLAG_ERR_MASK) == FLAG_ERR_MASK)
    {
        NON_FATAL_ERROR_ISR_DETAIL(FLAG_ERROR, err);
    }
}

void ADC_Ready_Interrupt(void)
{
    osThreadId_t *ADCTaskHandle = getOSThreadId();
    uint32_t err = osThreadFlagsSet(*ADCTaskHandle, READ_READY_FLAG);
    /*Detect any flag error states*/
    if ((err & FLAG_ERR_MASK) == FLAG_ERR_MASK)
    {
        NON_FATAL_ERROR_ISR_DETAIL(FLAG_ERROR, err);
    }
}

void configureADC(uint16_t configuration, const InputState_t *const input)
{
    uint16_t lowThreshold = 0;
    uint16_t highThreshold = 0xFFFF;

    uint8_t configBytes[2] = {0};
    configBytes[1] = (uint8_t)configuration;
    configBytes[0] = (uint8_t)(configuration >> BYTE_WIDTH);

    if (HAL_OK != HAL_I2C_Mem_Write_IT(&hi2c1, (uint16_t)((uint16_t)(input->adcAddress) << 1), ADC_LOW_THRESHOLD_REGISTER, sizeof(ADC_LOW_THRESHOLD_REGISTER), (uint8_t *)&lowThreshold, sizeof(lowThreshold)))
    {
        NON_FATAL_ERROR(I2C_BUS_ERROR);
    }
    if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(TRANSMIT_COMPLETE_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
    {
        NON_FATAL_ERROR(FLAG_ERROR);
    }

    if (HAL_OK != HAL_I2C_Mem_Write_IT(&hi2c1, (uint16_t)((uint16_t)(input->adcAddress) << 1), ADC_HIGH_THRESHOLD_REGISTER, sizeof(ADC_HIGH_THRESHOLD_REGISTER), (uint8_t *)&highThreshold, sizeof(highThreshold)))
    {
        NON_FATAL_ERROR(I2C_BUS_ERROR);
    }
    if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(TRANSMIT_COMPLETE_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
    {
        NON_FATAL_ERROR(FLAG_ERROR);
    }

    if (HAL_OK != HAL_I2C_Mem_Write_IT(&hi2c1, (uint16_t)((uint16_t)(input->adcAddress) << 1), ADC_CONFIG_REGISTER, sizeof(ADC_CONFIG_REGISTER), configBytes, sizeof(configBytes)))
    {
        NON_FATAL_ERROR(I2C_BUS_ERROR);
    }
    if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(TRANSMIT_COMPLETE_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
    {
        NON_FATAL_ERROR(FLAG_ERROR);
    }
}

/* Tasks */
static InputState_t *getInputState(uint8_t inputIdx)
{
    static InputState_t adcInput[ADC_COUNT] = {0};
    InputState_t *inputState = NULL;
    if (inputIdx >= ADC_COUNT)
    {
        NON_FATAL_ERROR(INVALID_ADC_NUMBER);
        inputState = &(adcInput[0]); /* A safe fallback */
    }
    else
    {
        inputState = &(adcInput[inputIdx]);
    }
    return inputState;
}

void ADCTask(void *arg) /* Yes this warns but it needs to be that way for matching the caller */
{
    const uint8_t INPUT_1 = 0;
    const uint8_t INPUT_2 = 1;
    const uint8_t INPUT_3 = 2;
    const uint8_t INPUT_4 = 3;

    InputState_t *adcInput1 = getInputState(INPUT_1);
    adcInput1->adcAddress = ADC1_ADDR;
    adcInput1->inputIndex = ADC_INPUT_1;

    InputState_t *adcInput2 = getInputState(INPUT_2);
    adcInput2->adcAddress = ADC1_ADDR;
    adcInput2->inputIndex = ADC_INPUT_2;

    InputState_t *adcInput3 = getInputState(INPUT_3);
    adcInput3->adcAddress = ADC2_ADDR;
    adcInput3->inputIndex = ADC_INPUT_1;

    InputState_t *adcInput4 = getInputState(INPUT_4);
    adcInput4->adcAddress = ADC2_ADDR;
    adcInput4->inputIndex = ADC_INPUT_2;

    for (uint8_t i = 0; i < ADC_COUNT; ++i)
    {
        InputState_t *adcInput = getInputState(i);
        adcInput->QInputValue = xQueueCreateStatic(1, sizeof(uint16_t), adcInput->QInputValue_Storage, &(adcInput->QInputValue_QueueStruct));
        adcInput->QInputTick = xQueueCreateStatic(1, sizeof(uint32_t), adcInput->QInputTicks_Storage, &(adcInput->QInputTicks_QueueStruct));

        QueueHandle_t *inputQueue = getInputQueue(i);
        *inputQueue = adcInput->QInputValue;

        QueueHandle_t *ticksQueue = getTicksQueue(i);
        *ticksQueue = adcInput->QInputTick;
    }

    while (true) /* Loop forever as we are an RTOS task */
    {
        for (uint8_t i = 0; i < ADC_COUNT; ++i)
        {
            InputState_t *adcInput = getInputState(i);

            /* Configure the ADC */
            uint16_t configuration = (uint16_t)((0 << 15) |                                          /* Operational status: noop */
                                                ((uint16_t)((uint16_t)adcInput->inputIndex << 12)) | /* ADC Input: Inputs are either 0b00 (input 0) or 0b11 (input 1) */
                                                (0b111 << 9) |                                       /* PGA: 16x */
                                                (1 << 8) |                                           /* Mode: single shot */
                                                (0b00 << 5) |                                        /* Data rate: 8SPS */
                                                (0 << 4) |                                           /* Comparator mode: traditional */
                                                (0 << 3) |                                           /* Comparator polarity: active low */
                                                (0 << 2) |                                           /* Comparator latch: non-latching */
                                                0);                                                  /* Comparator queue: 1 conversion */
            /* Configure for the next input */
            configureADC(configuration, adcInput); /* Will yield during I2C TX */

            /* Start the conversion */
            uint16_t triggerReadConfiguration = (uint16_t)((1 << 15) | configuration); /* Set the operation bit to start a conversion */
            uint8_t configBytes[2] = {0};
            configBytes[1] = (uint8_t)triggerReadConfiguration;
            configBytes[0] = (uint8_t)(triggerReadConfiguration >> BYTE_WIDTH);
            if (HAL_OK != HAL_I2C_Mem_Write_IT(&hi2c1, (uint16_t)((uint16_t)(adcInput->adcAddress) << 1), ADC_CONFIG_REGISTER, sizeof(ADC_CONFIG_REGISTER), configBytes, sizeof(configBytes)))
            {
                NON_FATAL_ERROR(I2C_BUS_ERROR);
            }

            if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(READ_READY_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
            {
                NON_FATAL_ERROR(FLAG_ERROR);
            }

            /* Read the ADC */
            uint8_t conversionRegister[2] = {0}; /* Memory space to receive inputs into */
            if (HAL_OK != HAL_I2C_Mem_Read_IT(&hi2c1, (uint16_t)((uint16_t)(adcInput->adcAddress) << 1), 0x00, 1, conversionRegister, sizeof(conversionRegister)))
            {
                NON_FATAL_ERROR(I2C_BUS_ERROR);
            }

            if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsWait(READ_COMPLETE_FLAG, osFlagsWaitAny, I2C_TIMEOUT)))
            {
                NON_FATAL_ERROR(FLAG_ERROR);
            }

            /* Export the value to the queue */
            uint16_t adcCounts = (uint16_t)((uint16_t)conversionRegister[0] << 8) | conversionRegister[1];
            uint32_t ticks = HAL_GetTick();
            bool valueWrite = xQueueOverwrite(adcInput->QInputValue, &adcCounts);
            bool tickWrite = false;
            if (valueWrite) /* Make sure our value got updated first, we don't want the ticks queue to lie about the currency of the data */
            {
                tickWrite = xQueueOverwrite(adcInput->QInputTick, &ticks);
            }
            else
            {
                NON_FATAL_ERROR(QUEUEING_ERROR);
            }

            /* Check the tick write before packing up */
            if (!tickWrite)
            {
                NON_FATAL_ERROR(QUEUEING_ERROR);
            }
        }
    }
}
