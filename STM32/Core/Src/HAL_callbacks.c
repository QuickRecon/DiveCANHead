#include "gpio.h"
#include "can.h"
#include "Sensors/AnalogOxygen.h"

extern void serial_printf(const char *fmt, ...);

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);
    if (GPIO_Pin == ADC1_ALERT_Pin)
    {
        // Trigger ADC1 read
        ADC_Ready_Interrupt(ADC1_ADDR);
    }
    else if (GPIO_Pin == ADC2_ALERT_Pin)
    {
        // Trigger ADC2 read
        ADC_Ready_Interrupt(ADC2_ADDR);
    }
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
    // serial_printf("I2C TX Callback\r\n");
    if (hi2c->Devaddress == (ADC1_ADDR << 1) || hi2c->Devaddress == (ADC2_ADDR << 1))
    {
        HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_RESET);
        ADC_I2C_Transmit_Complete(hi2c->Devaddress >> 1);
    }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
    // serial_printf("I2C RX Callback\r\n");
    if (hi2c->Devaddress == (ADC1_ADDR << 1) || hi2c->Devaddress == (ADC2_ADDR << 1))
    {
        ADC_I2C_Receive_Complete(hi2c->Devaddress >> 1, hi2c);
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
    CAN_RxHeaderTypeDef pRxHeader;
    uint8_t pData[64] = {0};
    HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &pRxHeader, pData);

    // Use 0x05 to reset into bootloader for flashing
    if (pRxHeader.StdId == 0x05)
    {
        JumpToBootloader();
    }
}