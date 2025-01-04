#include "main.h"
#include "Sensors/AnalogOxygen.h"
#include "Sensors/DigitalOxygen.h"
#include "Hardware/ext_adc.h"
#include "DiveCAN/Transciever.h"
#include "DiveCAN/DiveCAN.h"
#include "Hardware/printer.h"

extern const uint8_t ADC1_ADDR;
extern const uint8_t ADC2_ADDR;

const uint8_t BOOTLOADER_MSG = 0x79;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (ADC1_ALERT_Pin == GPIO_Pin)
    {
        /* Trigger ADC1 read */
        ADC1_Ready_Interrupt();
    }
    else if (ADC2_ALERT_Pin == GPIO_Pin)
    {
        /* Trigger ADC2 read */
        ADC2_Ready_Interrupt();
    }
    else
    {
        /* Do nothing, we don't care about this pin (yet) */
    }
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if ((hi2c->Devaddress == ((uint32_t)ADC1_ADDR << 1)) || (hi2c->Devaddress == ((uint32_t)ADC2_ADDR << 1)))
    {
        ADC_I2C_Transmit_Complete();
    }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if ((hi2c->Devaddress == ((uint32_t)ADC1_ADDR << 1)) || (hi2c->Devaddress == ((uint32_t)ADC2_ADDR << 1)))
    {
        ADC_I2C_Receive_Complete();
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    Cell_RX_Complete(huart, size);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    Cell_TX_Complete(huart);
}

void HAL_CAN_RxMsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
    CAN_RxHeaderTypeDef pRxHeader = {0};
    uint8_t pData[64] = {0};
    (void)HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &pRxHeader, pData);

    /* Use 0x79 to reset into bootloader for flashing, because
     * it is a no-op in both DiveCAN land and bootloader land
     */
    if (BOOTLOADER_MSG == pRxHeader.StdId)
    {
        JumpToBootloader();
    }
    else
    {
        rxInterrupt(pRxHeader.ExtId, (uint8_t)pRxHeader.DLC, pData);
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_RxMsgPendingCallback(hcan);
}

void HAL_CAN_RxFifo1MsgPendingCallbackxFifo1(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_RxMsgPendingCallback(hcan);
}
