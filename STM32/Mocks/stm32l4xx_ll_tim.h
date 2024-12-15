/**
 ******************************************************************************
 * @file    stm32l4xx_hal_tim.h
 * @author  MCD Application Team
 * @brief   Header file of TIM HAL module.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2017 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef STM32L4xx_HAL_TIM_H
#define STM32L4xx_HAL_TIM_H
#include <stddef.h>
#include <stm32l4xx_hal_def.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    uint32_t Prescaler; /*!< Specifies the prescaler value used to divide the TIM clock.
                             This parameter can be a number between Min_Data = 0x0000 and Max_Data = 0xFFFF */

    uint32_t CounterMode; /*!< Specifies the counter mode.
                               This parameter can be a value of @ref TIM_Counter_Mode */

    uint32_t Period; /*!< Specifies the period value to be loaded into the active
                          Auto-Reload Register at the next update event.
                          This parameter can be a number between Min_Data = 0x0000 and Max_Data = 0xFFFF.  */

    uint32_t ClockDivision; /*!< Specifies the clock division.
                                 This parameter can be a value of @ref TIM_ClockDivision */

    uint32_t RepetitionCounter; /*!< Specifies the repetition counter value. Each time the RCR downcounter
                                     reaches zero, an update event is generated and counting restarts
                                     from the RCR value (N).
                                     This means in PWM mode that (N+1) corresponds to:
                                         - the number of PWM periods in edge-aligned mode
                                         - the number of half PWM period in center-aligned mode
                                      GP timers: this parameter must be a number between Min_Data = 0x00 and
                                      Max_Data = 0xFF.
                                      Advanced timers: this parameter must be a number between Min_Data = 0x0000 and
                                      Max_Data = 0xFFFF. */

    uint32_t AutoReloadPreload; /*!< Specifies the auto-reload preload.
                                    This parameter can be a value of @ref TIM_AutoReloadPreload */
  } TIM_Base_InitTypeDef;

  typedef struct
  {
    uint32_t CR1;   /*!< TIM control register 1,                   Address offset: 0x00 */
    uint32_t CR2;   /*!< TIM control register 2,                   Address offset: 0x04 */
    uint32_t SMCR;  /*!< TIM slave mode control register,          Address offset: 0x08 */
    uint32_t DIER;  /*!< TIM DMA/interrupt enable register,        Address offset: 0x0C */
    uint32_t SR;    /*!< TIM status register,                      Address offset: 0x10 */
    uint32_t EGR;   /*!< TIM event generation register,            Address offset: 0x14 */
    uint32_t CCMR1; /*!< TIM capture/compare mode register 1,      Address offset: 0x18 */
    uint32_t CCMR2; /*!< TIM capture/compare mode register 2,      Address offset: 0x1C */
    uint32_t CCER;  /*!< TIM capture/compare enable register,      Address offset: 0x20 */
    uint32_t CNT;   /*!< TIM counter register,                     Address offset: 0x24 */
    uint32_t PSC;   /*!< TIM prescaler,                            Address offset: 0x28 */
    uint32_t ARR;   /*!< TIM auto-reload register,                 Address offset: 0x2C */
    uint32_t RCR;   /*!< TIM repetition counter register,          Address offset: 0x30 */
    uint32_t CCR1;  /*!< TIM capture/compare register 1,           Address offset: 0x34 */
    uint32_t CCR2;  /*!< TIM capture/compare register 2,           Address offset: 0x38 */
    uint32_t CCR3;  /*!< TIM capture/compare register 3,           Address offset: 0x3C */
    uint32_t CCR4;  /*!< TIM capture/compare register 4,           Address offset: 0x40 */
    uint32_t BDTR;  /*!< TIM break and dead-time register,         Address offset: 0x44 */
    uint32_t DCR;   /*!< TIM DMA control register,                 Address offset: 0x48 */
    uint32_t DMAR;  /*!< TIM DMA address for full transfer,        Address offset: 0x4C */
    uint32_t OR1;   /*!< TIM option register 1,                    Address offset: 0x50 */
    uint32_t CCMR3; /*!< TIM capture/compare mode register 3,      Address offset: 0x54 */
    uint32_t CCR5;  /*!< TIM capture/compare register5,            Address offset: 0x58 */
    uint32_t CCR6;  /*!< TIM capture/compare register6,            Address offset: 0x5C */
    uint32_t OR2;   /*!< TIM option register 2,                    Address offset: 0x60 */
    uint32_t OR3;   /*!< TIM option register 3,                    Address offset: 0x64 */
  } TIM_TypeDef;

  typedef enum
  {
    HAL_TIM_ACTIVE_CHANNEL_1 = 0x01U,      /*!< The active channel is 1     */
    HAL_TIM_ACTIVE_CHANNEL_2 = 0x02U,      /*!< The active channel is 2     */
    HAL_TIM_ACTIVE_CHANNEL_3 = 0x04U,      /*!< The active channel is 3     */
    HAL_TIM_ACTIVE_CHANNEL_4 = 0x08U,      /*!< The active channel is 4     */
    HAL_TIM_ACTIVE_CHANNEL_5 = 0x10U,      /*!< The active channel is 5     */
    HAL_TIM_ACTIVE_CHANNEL_6 = 0x20U,      /*!< The active channel is 6     */
    HAL_TIM_ACTIVE_CHANNEL_CLEARED = 0x00U /*!< All active channels cleared */
  } HAL_TIM_ActiveChannel;
  typedef struct
  {
    uint32_t CCR;   /*!< DMA channel x configuration register        */
    uint32_t CNDTR; /*!< DMA channel x number of data register       */
    uint32_t CPAR;  /*!< DMA channel x peripheral address register   */
    uint32_t CMAR;  /*!< DMA channel x memory address register       */
  } DMA_Channel_TypeDef;

  typedef struct
  {
    uint32_t ISR;  /*!< DMA interrupt status register,                 Address offset: 0x00 */
    uint32_t IFCR; /*!< DMA interrupt flag clear register,             Address offset: 0x04 */
  } DMA_TypeDef;

  typedef struct
  {
    uint32_t CSELR; /*!< DMA channel selection register              */
  } DMA_Request_TypeDef;

  typedef struct
  {
    uint32_t Request; /*!< Specifies the request selected for the specified channel.
                           This parameter can be a value of @ref DMA_request */

    uint32_t Direction; /*!< Specifies if the data will be transferred from memory to peripheral,
                             from memory to memory or from peripheral to memory.
                             This parameter can be a value of @ref DMA_Data_transfer_direction */

    uint32_t PeriphInc; /*!< Specifies whether the Peripheral address register should be incremented or not.
                             This parameter can be a value of @ref DMA_Peripheral_incremented_mode */

    uint32_t MemInc; /*!< Specifies whether the memory address register should be incremented or not.
                          This parameter can be a value of @ref DMA_Memory_incremented_mode */

    uint32_t PeriphDataAlignment; /*!< Specifies the Peripheral data width.
                                       This parameter can be a value of @ref DMA_Peripheral_data_size */

    uint32_t MemDataAlignment; /*!< Specifies the Memory data width.
                                    This parameter can be a value of @ref DMA_Memory_data_size */

    uint32_t Mode; /*!< Specifies the operation mode of the DMAy Channelx.
                        This parameter can be a value of @ref DMA_mode
                        @note The circular buffer mode cannot be used if the memory-to-memory
                              data transfer is configured on the selected Channel */

    uint32_t Priority; /*!< Specifies the software priority for the DMAy Channelx.
                            This parameter can be a value of @ref DMA_Priority_level */
  } DMA_InitTypeDef;

  typedef enum
  {
    HAL_DMA_STATE_RESET = 0x00U,   /*!< DMA not yet initialized or disabled    */
    HAL_DMA_STATE_READY = 0x01U,   /*!< DMA initialized and ready for use      */
    HAL_DMA_STATE_BUSY = 0x02U,    /*!< DMA process is ongoing                 */
    HAL_DMA_STATE_TIMEOUT = 0x03U, /*!< DMA timeout state                      */
  } HAL_DMA_StateTypeDef;

  typedef struct __DMA_HandleTypeDef
  {
    DMA_Channel_TypeDef *Instance; /*!< Register base address                */

    DMA_InitTypeDef Init; /*!< DMA communication parameters         */

    HAL_LockTypeDef Lock; /*!< DMA locking object                   */

    HAL_DMA_StateTypeDef State; /*!< DMA transfer state                   */

    void *Parent; /*!< Parent object state                  */

    void (*XferCpltCallback)(struct __DMA_HandleTypeDef *hdma); /*!< DMA transfer complete callback       */

    void (*XferHalfCpltCallback)(struct __DMA_HandleTypeDef *hdma); /*!< DMA Half transfer complete callback  */

    void (*XferErrorCallback)(struct __DMA_HandleTypeDef *hdma); /*!< DMA transfer error callback          */

    void (*XferAbortCallback)(struct __DMA_HandleTypeDef *hdma); /*!< DMA transfer abort callback          */

    uint32_t ErrorCode; /*!< DMA Error code                       */

    DMA_TypeDef *DmaBaseAddress; /*!< DMA Channel Base Address             */

    uint32_t ChannelIndex; /*!< DMA Channel Index                    */

#if defined(DMAMUX1)
    DMAMUX_Channel_TypeDef *DMAmuxChannel; /*!< Register base address                */

    DMAMUX_ChannelStatus_TypeDef *DMAmuxChannelStatus; /*!< DMAMUX Channels Status Base Address  */

    uint32_t DMAmuxChannelStatusMask; /*!< DMAMUX Channel Status Mask           */

    DMAMUX_RequestGen_TypeDef *DMAmuxRequestGen; /*!< DMAMUX request generator Base Address */

    DMAMUX_RequestGenStatus_TypeDef *DMAmuxRequestGenStatus; /*!< DMAMUX request generator Address     */

    uint32_t DMAmuxRequestGenStatusMask; /*!< DMAMUX request generator Status mask */

#endif /* DMAMUX1 */

  } DMA_HandleTypeDef;

  typedef enum
  {
    HAL_TIM_STATE_RESET = 0x00U,   /*!< Peripheral not yet initialized or disabled  */
    HAL_TIM_STATE_READY = 0x01U,   /*!< Peripheral Initialized and ready for use    */
    HAL_TIM_STATE_BUSY = 0x02U,    /*!< An internal process is ongoing              */
    HAL_TIM_STATE_TIMEOUT = 0x03U, /*!< Timeout state                               */
    HAL_TIM_STATE_ERROR = 0x04U    /*!< Reception process is ongoing                */
  } HAL_TIM_StateTypeDef;

  /**
   * @brief  TIM Channel States definition
   */
  typedef enum
  {
    HAL_TIM_CHANNEL_STATE_RESET = 0x00U, /*!< TIM Channel initial state                         */
    HAL_TIM_CHANNEL_STATE_READY = 0x01U, /*!< TIM Channel ready for use                         */
    HAL_TIM_CHANNEL_STATE_BUSY = 0x02U,  /*!< An internal process is ongoing on the TIM channel */
  } HAL_TIM_ChannelStateTypeDef;

  /**
   * @brief  DMA Burst States definition
   */
  typedef enum
  {
    HAL_DMA_BURST_STATE_RESET = 0x00U, /*!< DMA Burst initial state */
    HAL_DMA_BURST_STATE_READY = 0x01U, /*!< DMA Burst ready for use */
    HAL_DMA_BURST_STATE_BUSY = 0x02U,  /*!< Ongoing DMA Burst       */
  } HAL_TIM_DMABurstStateTypeDef;

#if (USE_HAL_TIM_REGISTER_CALLBACKS == 1)
  typedef struct __TIM_HandleTypeDef
#else
typedef struct
#endif /* USE_HAL_TIM_REGISTER_CALLBACKS */
  {
    TIM_TypeDef *Instance;                        /*!< Register base address                             */
    TIM_Base_InitTypeDef Init;                    /*!< TIM Time Base required parameters                 */
    HAL_TIM_ActiveChannel Channel;                /*!< Active channel                                    */
    DMA_HandleTypeDef *hdma[7];                   /*!< DMA Handlers array
                                                       This array is accessed by a @ref DMA_Handle_index */
    HAL_LockTypeDef Lock;                         /*!< Locking object                                    */
    HAL_TIM_StateTypeDef State;                   /*!< TIM operation state                               */
    HAL_TIM_ChannelStateTypeDef ChannelState[6];  /*!< TIM channel operation state                       */
    HAL_TIM_ChannelStateTypeDef ChannelNState[4]; /*!< TIM complementary channel operation state         */
    HAL_TIM_DMABurstStateTypeDef DMABurstState;   /*!< DMA burst operation state                         */

#if (USE_HAL_TIM_REGISTER_CALLBACKS == 1)
    void (*Base_MspInitCallback)(struct __TIM_HandleTypeDef *htim);              /*!< TIM Base Msp Init Callback                              */
    void (*Base_MspDeInitCallback)(struct __TIM_HandleTypeDef *htim);            /*!< TIM Base Msp DeInit Callback                            */
    void (*IC_MspInitCallback)(struct __TIM_HandleTypeDef *htim);                /*!< TIM IC Msp Init Callback                                */
    void (*IC_MspDeInitCallback)(struct __TIM_HandleTypeDef *htim);              /*!< TIM IC Msp DeInit Callback                              */
    void (*OC_MspInitCallback)(struct __TIM_HandleTypeDef *htim);                /*!< TIM OC Msp Init Callback                                */
    void (*OC_MspDeInitCallback)(struct __TIM_HandleTypeDef *htim);              /*!< TIM OC Msp DeInit Callback                              */
    void (*PWM_MspInitCallback)(struct __TIM_HandleTypeDef *htim);               /*!< TIM PWM Msp Init Callback                               */
    void (*PWM_MspDeInitCallback)(struct __TIM_HandleTypeDef *htim);             /*!< TIM PWM Msp DeInit Callback                             */
    void (*OnePulse_MspInitCallback)(struct __TIM_HandleTypeDef *htim);          /*!< TIM One Pulse Msp Init Callback                         */
    void (*OnePulse_MspDeInitCallback)(struct __TIM_HandleTypeDef *htim);        /*!< TIM One Pulse Msp DeInit Callback                       */
    void (*Encoder_MspInitCallback)(struct __TIM_HandleTypeDef *htim);           /*!< TIM Encoder Msp Init Callback                           */
    void (*Encoder_MspDeInitCallback)(struct __TIM_HandleTypeDef *htim);         /*!< TIM Encoder Msp DeInit Callback                         */
    void (*HallSensor_MspInitCallback)(struct __TIM_HandleTypeDef *htim);        /*!< TIM Hall Sensor Msp Init Callback                       */
    void (*HallSensor_MspDeInitCallback)(struct __TIM_HandleTypeDef *htim);      /*!< TIM Hall Sensor Msp DeInit Callback                     */
    void (*PeriodElapsedCallback)(struct __TIM_HandleTypeDef *htim);             /*!< TIM Period Elapsed Callback                             */
    void (*PeriodElapsedHalfCpltCallback)(struct __TIM_HandleTypeDef *htim);     /*!< TIM Period Elapsed half complete Callback               */
    void (*TriggerCallback)(struct __TIM_HandleTypeDef *htim);                   /*!< TIM Trigger Callback                                    */
    void (*TriggerHalfCpltCallback)(struct __TIM_HandleTypeDef *htim);           /*!< TIM Trigger half complete Callback                      */
    void (*IC_CaptureCallback)(struct __TIM_HandleTypeDef *htim);                /*!< TIM Input Capture Callback                              */
    void (*IC_CaptureHalfCpltCallback)(struct __TIM_HandleTypeDef *htim);        /*!< TIM Input Capture half complete Callback                */
    void (*OC_DelayElapsedCallback)(struct __TIM_HandleTypeDef *htim);           /*!< TIM Output Compare Delay Elapsed Callback               */
    void (*PWM_PulseFinishedCallback)(struct __TIM_HandleTypeDef *htim);         /*!< TIM PWM Pulse Finished Callback                         */
    void (*PWM_PulseFinishedHalfCpltCallback)(struct __TIM_HandleTypeDef *htim); /*!< TIM PWM Pulse Finished half complete Callback           */
    void (*ErrorCallback)(struct __TIM_HandleTypeDef *htim);                     /*!< TIM Error Callback                                      */
    void (*CommutationCallback)(struct __TIM_HandleTypeDef *htim);               /*!< TIM Commutation Callback                                */
    void (*CommutationHalfCpltCallback)(struct __TIM_HandleTypeDef *htim);       /*!< TIM Commutation half complete Callback                  */
    void (*BreakCallback)(struct __TIM_HandleTypeDef *htim);                     /*!< TIM Break Callback                                      */
    void (*Break2Callback)(struct __TIM_HandleTypeDef *htim);                    /*!< TIM Break2 Callback                                     */
#endif                                                                           /* USE_HAL_TIM_REGISTER_CALLBACKS */
  } TIM_HandleTypeDef;

  /**
   * @}
   */
  /* End of private functions --------------------------------------------------*/

  /**
   * @}
   */

  /**
   * @}
   */

#ifdef __cplusplus
}
#endif

#endif /* STM32L4xx_HAL_TIM_H */
