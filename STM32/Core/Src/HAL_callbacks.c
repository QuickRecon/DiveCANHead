#include "gpio.h"
#include "can.h"
#include "Sensors/AnalogOxygen.h"
#include "Sensors/DigitalOxygen.h"
#include "Hardware/ext_adc.h"

extern const uint8_t ADC1_ADDR;
extern const uint8_t ADC2_ADDR;
extern void serial_printf(const char *fmt, ...);

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);
    if (ADC1_ALERT_Pin == GPIO_Pin)
    {
        // Trigger ADC1 read
        ADC_Ready_Interrupt(ADC1_ADDR);
    }
    else if (ADC2_ALERT_Pin == GPIO_Pin)
    {
        // Trigger ADC2 read
        ADC_Ready_Interrupt(ADC2_ADDR);
    } else {
        // Do nothing, we don't care about this pin (yet)
    }
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
    if ((hi2c->Devaddress == ((uint32_t)ADC1_ADDR << 1)) || (hi2c->Devaddress == ((uint32_t)ADC2_ADDR << 1)))
    {
        HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_RESET);
        ADC_I2C_Transmit_Complete((uint8_t)(hi2c->Devaddress >> 1));
    }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
    if ((hi2c->Devaddress == ((uint32_t)ADC1_ADDR << 1)) || (hi2c->Devaddress == ((uint32_t)ADC2_ADDR << 1)))
    {
        ADC_I2C_Receive_Complete((uint8_t)(hi2c->Devaddress >> 1), hi2c);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart,  uint16_t size){
    //serial_printf("Size: %d\n", size);
    Cell_RX_Complete(huart, size);
}

// void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef* huart){
//     Cell_RX_Complete(huart);
// }

// void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart){
//     Cell_RX_Complete(huart);
// }

void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart){
    Cell_TX_Complete(huart);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
    CAN_RxHeaderTypeDef pRxHeader = {0};
    uint8_t pData[64] = {0};
    HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &pRxHeader, pData);

    // Use 0x05 to reset into bootloader for flashing
    if (0x05 == pRxHeader.StdId)
    {
        JumpToBootloader();
    }
}