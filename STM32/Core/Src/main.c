/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "eeprom_emul.h"
#include "Sensors/OxygenCell.h"
#include "Sensors/AnalogOxygen.h"
#include "Hardware/pwr_management.h"
#include "Hardware/solenoid.h"
#include "Hardware/ext_adc.h"
#include "Hardware/printer.h"
#include "Hardware/flash.h"
#include "DiveCAN/DiveCAN.h"
#include "DiveCAN/PPO2Transmitter.h"
#include "Hardware/log.h"
#include "configuration.h"
#include "PPO2Control/PPO2Control.h"
#include "Hardware/hw_version.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
const uint8_t IRQ_PRIORITY_DEFAULT = 5;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

CAN_HandleTypeDef hcan1;

CRC_HandleTypeDef hcrc;

I2C_HandleTypeDef hi2c1;

IWDG_HandleTypeDef hiwdg;

SD_HandleTypeDef hsd1;
DMA_HandleTypeDef hdma_sdmmc1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim7;
TIM_HandleTypeDef htim15;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* Definitions for watchdogTask */
osThreadId_t watchdogTaskHandle;
uint32_t watchdogTaskBuffer[128];
osStaticThreadDef_t watchdogTaskControlBlock;
const osThreadAttr_t watchdogTask_attributes = {
    .name = "watchdogTask",
    .cb_mem = &watchdogTaskControlBlock,
    .cb_size = sizeof(watchdogTaskControlBlock),
    .stack_mem = &watchdogTaskBuffer[0],
    .stack_size = sizeof(watchdogTaskBuffer),
    .priority = (osPriority_t)osPriorityLow,
};
/* Definitions for sDInitTask */
osThreadId_t sDInitTaskHandle;
uint32_t SDInitTaskBuffer[256];
osStaticThreadDef_t SDInitTaskControlBlock;
const osThreadAttr_t sDInitTask_attributes = {
    .name = "sDInitTask",
    .cb_mem = &SDInitTaskControlBlock,
    .cb_size = sizeof(SDInitTaskControlBlock),
    .stack_mem = &SDInitTaskBuffer[0],
    .stack_size = sizeof(SDInitTaskBuffer),
    .priority = (osPriority_t)osPriorityRealtime,
};
/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_CAN1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_SDMMC1_SD_Init(void);
static void MX_CRC_Init(void);
static void MX_TIM7_Init(void);
static void MX_IWDG_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM15_Init(void);
void WatchdogTask(void *argument);
void SDInitTask(void *argument);

static void MX_NVIC_Init(void);
/* USER CODE BEGIN PFP */

/** @brief  Possible STM32 system reset causes
 */
typedef enum
{
  RESET_CAUSE_UNKNOWN = 0,
  RESET_CAUSE_LOW_POWER_RESET,
  RESET_CAUSE_WNDW_WTCHDOG_RESET,
  RESET_CAUSE_I_WTCHDOG_RESET,
  RESET_CAUSE_SOFTWARE_RESET,
  RESET_CAUSE_FIREWALL_RESET,
  RESET_CAUSE_EXT_PIN_RESET,
  RESET_CAUSE_BROWNOUT_RESET,
} reset_cause_t;

/** @brief      Obtain the STM32 system reset cause
 ** @param      None
 ** @return     The system reset cause
 */
reset_cause_t reset_cause_get(void)
{
  reset_cause_t reset_cause = RESET_CAUSE_UNKNOWN;

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != 0)
  {
    reset_cause = RESET_CAUSE_LOW_POWER_RESET;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != 0)
  {
    reset_cause = RESET_CAUSE_WNDW_WTCHDOG_RESET;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0)
  {
    reset_cause = RESET_CAUSE_I_WTCHDOG_RESET;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0)
  {
    /* This reset is induced by calling the ARM CMSIS */
    /* `NVIC_SystemReset()` function! */
    reset_cause = RESET_CAUSE_SOFTWARE_RESET;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_FWRST) != 0)
  {
    reset_cause = RESET_CAUSE_FIREWALL_RESET;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0)
  {
    reset_cause = RESET_CAUSE_EXT_PIN_RESET;
  }
  /* Needs to come *after* checking the `RCC_FLAG_PORRST` flag in order to */
  /* ensure first that the reset cause is NOT a POR/PDR reset. See note */
  /* below. */
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0)
  {
    reset_cause = RESET_CAUSE_BROWNOUT_RESET;
  }
  else
  {
    reset_cause = RESET_CAUSE_UNKNOWN;
  }

  /* Clear all the reset flags or else they will remain set during future */
  /* resets until system power is fully removed. */
  (void)__HAL_RCC_CLEAR_RESET_FLAGS();

  return reset_cause;
}

/* Note: any of the STM32 Hardware Abstraction Layer (HAL) Reset and Clock
 * Controller (RCC) header files, such as
 * "STM32Cube_FW_F7_V1.12.0/Drivers/STM32F7xx_HAL_Driver/Inc/stm32f7xx_hal_rcc.h",
 * "STM32Cube_FW_F2_V1.7.0/Drivers/STM32F2xx_HAL_Driver/Inc/stm32f2xx_hal_rcc.h",
 * etc., indicate that the brownout flag, `RCC_FLAG_BORRST`, will be set in
 * the event of a "POR/PDR or BOR reset". This means that a Power-On Reset
 * (POR), Power-Down Reset (PDR), OR Brownout Reset (BOR) will trip this flag.
 * See the doxygen just above their definition for the
 * `__HAL_RCC_GET_FLAG()` macro to see this:
 *      "@arg RCC_FLAG_BORRST: POR/PDR or BOR reset." <== indicates the Brownout
 *      Reset flag will *also* be set in the event of a POR/PDR.
 * Therefore, you must check the Brownout Reset flag, `RCC_FLAG_BORRST`, *after*
 * first checking the `RCC_FLAG_PORRST` flag in order to ensure first that the
 * reset cause is NOT a POR/PDR reset. */

/** @brief      Obtain the system reset cause as an ASCII-printable name string
 *             from a reset cause type
 *  @param[in]  reset_cause     The previously-obtained system reset cause
 * @return     A null-terminated ASCII name string describing the system
 *             reset cause
 */
const char *reset_cause_get_name(reset_cause_t reset_cause)
{
  const char *reset_cause_name = NULL;

  switch (reset_cause)
  {
  case RESET_CAUSE_UNKNOWN:
    reset_cause_name = "UNKNOWN";
    break;
  case RESET_CAUSE_LOW_POWER_RESET:
    reset_cause_name = "LOW_POWER_RESET";
    break;
  case RESET_CAUSE_WNDW_WTCHDOG_RESET:
    reset_cause_name = "WINDOW_WATCHDOG_RESET";
    break;
  case RESET_CAUSE_I_WTCHDOG_RESET:
    reset_cause_name = "INDEPENDENT_WATCHDOG_RESET";
    break;
  case RESET_CAUSE_SOFTWARE_RESET:
    reset_cause_name = "SOFTWARE_RESET";
    break;
  case RESET_CAUSE_FIREWALL_RESET:
    reset_cause_name = "FIREWALL_RESET";
    break;
  case RESET_CAUSE_EXT_PIN_RESET:
    reset_cause_name = "EXTERNAL_RESET_PIN_RESET";
    break;
  case RESET_CAUSE_BROWNOUT_RESET:
    reset_cause_name = "BROWNOUT_RESET (BOR)";
    break;
  default:
    reset_cause_name = "UNDEFINED";
  }

  return reset_cause_name;
}

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief Toggle pin if enabled is GPIO_PIN_SET
 * @param enabled Whether this pin should be toggled
 * @param GPIOx Port to toggle
 * @param GPIO_Pin Pin on port to toggle
 */
inline static void toggle_blink_pin(GPIO_PinState enabled, GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  if (GPIO_PIN_SET == enabled)
  {
    HAL_GPIO_WritePin(GPIOx, GPIO_Pin, !(bool)HAL_GPIO_ReadPin(GPIOx, GPIO_Pin));
  }
}

/**
 * @brief Blink the current boot sequence LEDs, determined from the output state so can be triggered in any context
 */

[[noreturn]] static void boot_fail_blink(void)
{

  GPIO_PinState LED0_GPIO_State = HAL_GPIO_ReadPin(LED0_GPIO_Port, LED0_Pin);
  GPIO_PinState LED1_GPIO_State = HAL_GPIO_ReadPin(LED1_GPIO_Port, LED1_Pin);
  GPIO_PinState LED2_GPIO_State = HAL_GPIO_ReadPin(LED2_GPIO_Port, LED2_Pin);
  GPIO_PinState LED3_GPIO_State = HAL_GPIO_ReadPin(LED3_GPIO_Port, LED3_Pin);
  GPIO_PinState LED4_GPIO_State = HAL_GPIO_ReadPin(LED4_GPIO_Port, LED4_Pin);
  GPIO_PinState LED5_GPIO_State = HAL_GPIO_ReadPin(LED5_GPIO_Port, LED5_Pin);
  GPIO_PinState LED6_GPIO_State = HAL_GPIO_ReadPin(LED6_GPIO_Port, LED6_Pin);
  GPIO_PinState LED7_GPIO_State = HAL_GPIO_ReadPin(LED7_GPIO_Port, LED7_Pin);

  /* We DO NOT reset the watchdog in this loop so that we can attempt to blink for a while before we get reset*/
  while (true)
  {
    toggle_blink_pin(LED0_GPIO_State, LED0_GPIO_Port, LED0_Pin);
    toggle_blink_pin(LED1_GPIO_State, LED1_GPIO_Port, LED1_Pin);
    toggle_blink_pin(LED2_GPIO_State, LED2_GPIO_Port, LED2_Pin);
    toggle_blink_pin(LED3_GPIO_State, LED3_GPIO_Port, LED3_Pin);
    toggle_blink_pin(LED4_GPIO_State, LED4_GPIO_Port, LED4_Pin);
    toggle_blink_pin(LED5_GPIO_State, LED5_GPIO_Port, LED5_Pin);
    toggle_blink_pin(LED6_GPIO_State, LED6_GPIO_Port, LED6_Pin);
    toggle_blink_pin(LED7_GPIO_State, LED7_GPIO_Port, LED7_Pin);
  }
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{

  /* USER CODE BEGIN 1 */
  const reset_cause_t reset_cause = reset_cause_get();
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_CAN1_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_SDMMC1_SD_Init();
  MX_FATFS_Init();
  MX_CRC_Init();
  MX_TIM7_Init();
  MX_IWDG_Init();
  MX_TIM1_Init();
  MX_TIM15_Init();

  /* Initialize interrupts */
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED5_GPIO_Port, LED5_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED6_GPIO_Port, LED6_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_SET);

  /* Detect our hardware version */
  const HW_Version_t HARDWARE_VERSION = get_hardware_version();
  if (HW_INVALID == HARDWARE_VERSION)
  {
    boot_fail_blink();
  }

  /* Ensure solenoid is fully off */
  setSolenoidOff();
  HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_RESET);
  (void)HAL_IWDG_Refresh(&hiwdg);

  InitLog();
  HAL_GPIO_WritePin(LED6_GPIO_Port, LED6_Pin, GPIO_PIN_RESET);
  (void)HAL_IWDG_Refresh(&hiwdg);

  /* Set up our eeprom emulation and assert our option bytes */
  initFlash();

  /* Load Config */
  const Configuration_t deviceConfig = loadConfiguration(HARDWARE_VERSION);
  HAL_GPIO_WritePin(LED5_GPIO_Port, LED5_Pin, GPIO_PIN_RESET);
  (void)HAL_IWDG_Refresh(&hiwdg);

  InitPrinter(deviceConfig.enableUartPrinting && deviceConfig.extendedMessages);
  serial_printf("Booting, Last Reset Reason (%s)\r\n", reset_cause_get_name(reset_cause));
  serial_printf("Configuration: 0x%lx\r\n", getConfigBytes(&deviceConfig));

  /* Set our power bus */
  SetVBusMode(deviceConfig.powerMode);
  HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);
  (void)HAL_IWDG_Refresh(&hiwdg);

  /* Kick off our threads */
  InitADCs();

  QueueHandle_t cells[3] = {0};
  cells[CELL_1] = CreateCell(CELL_1, deviceConfig.cell1);
  cells[CELL_2] = CreateCell(CELL_2, deviceConfig.cell2);
  cells[CELL_3] = CreateCell(CELL_3, deviceConfig.cell3);
  HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
  (void)HAL_IWDG_Refresh(&hiwdg);

  /* Set up our CAN BUS*/
  const char *device_name = "";
  switch (HARDWARE_VERSION)
  {
  case HW_REV_2_2:
    device_name = "REV2.2";
    break;
  case HW_REV_2_3:
    device_name = "REV2.3";
    break;
  case HW_JR:
    device_name = "DC_JR";
    break;
  default:
    device_name = "UNKNOWN";
  }

  const DiveCANDevice_t defaultDeviceSpec = {
      .name = device_name,
      .type = DIVECAN_SOLO,
      .manufacturerID = DIVECAN_MANUFACTURER_SRI,
      .firmwareVersion = FIRMWARE_VERSION,
      .hardwareVersion = HARDWARE_VERSION};

  InitDiveCAN(&defaultDeviceSpec, &deviceConfig);
  InitPPO2TX(&defaultDeviceSpec, cells[CELL_1], cells[CELL_2], cells[CELL_3]);
  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
  (void)HAL_IWDG_Refresh(&hiwdg);

  InitPPO2ControlLoop(cells[CELL_1], cells[CELL_2], cells[CELL_3], deviceConfig.ppo2DepthCompensation, deviceConfig.extendedMessages, deviceConfig.ppo2controlMode);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of watchdogTask */
  watchdogTaskHandle = osThreadNew(WatchdogTask, NULL, &watchdogTask_attributes);

  /* creation of sDInitTask */
  sDInitTaskHandle = osThreadNew(SDInitTask, NULL, &sDInitTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */

  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
  (void)HAL_IWDG_Refresh(&hiwdg);

  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 12;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV4;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV8;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enables the Clock Security System
   */
  HAL_RCC_EnableCSS();
}

/**
 * @brief NVIC Configuration.
 * @retval None
 */
static void MX_NVIC_Init(void)
{
  /* SDMMC1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SDMMC1_IRQn, IRQ_PRIORITY_DEFAULT, 0);
  HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
  /* I2C1_EV_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(I2C1_EV_IRQn, IRQ_PRIORITY_DEFAULT, 0);
  HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
  /* EXTI15_10_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, IRQ_PRIORITY_DEFAULT, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
  /* CAN1_RX0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(CAN1_RX0_IRQn, IRQ_PRIORITY_DEFAULT, 0);
  HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
  /* CAN1_RX1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(CAN1_RX1_IRQn, IRQ_PRIORITY_DEFAULT, 0);
  HAL_NVIC_EnableIRQ(CAN1_RX1_IRQn);
  /* USART2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(USART2_IRQn, IRQ_PRIORITY_DEFAULT, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USART3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(USART3_IRQn, IRQ_PRIORITY_DEFAULT, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
  /* USART1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(USART1_IRQn, IRQ_PRIORITY_DEFAULT, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
   */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
   */
  sConfig.Channel = ADC_CHANNEL_VREFINT;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  if (HAL_ADC_Stop(&hadc1) != HAL_OK)
  {
    NON_FATAL_ERROR(CRITICAL_ERR);
  }
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    NON_FATAL_ERROR(CRITICAL_ERR);
  }
  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    NON_FATAL_ERROR(CRITICAL_ERR);
  }
  /* USER CODE END ADC1_Init 2 */
}

/**
 * @brief CAN1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 6;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_4TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = ENABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */
  CAN_FilterTypeDef sFilterConfig = {0};
  sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0; /* set fifo assignment */
  sFilterConfig.FilterIdHigh = 0;
  sFilterConfig.FilterIdLow = 0;
  sFilterConfig.FilterMaskIdHigh = 0;
  sFilterConfig.FilterMaskIdLow = 0;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT; /* set filter scale */
  sFilterConfig.FilterActivation = ENABLE;
  (void)HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig);
  (void)HAL_CAN_Start(&hcan1);                                             /* start CAN */
  (void)HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING); /* enable interrupts */
  /* USER CODE END CAN1_Init 2 */
}

/**
 * @brief CRC Initialization Function
 * @param None
 * @retval None
 */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */
}

/**
 * @brief I2C1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00402D41;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
   */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
   */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */
}

/**
 * @brief IWDG Initialization Function
 * @param None
 * @retval None
 */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
  hiwdg.Init.Window = 4095;
  hiwdg.Init.Reload = 4095;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */
  /* Freeze the IWDG on debug so we don't need to disable it as a matter of routine */
  __HAL_DBGMCU_FREEZE_IWDG();
  /* USER CODE END IWDG_Init 2 */
}

/**
 * @brief SDMMC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_SDMMC1_SD_Init(void)
{

  /* USER CODE BEGIN SDMMC1_Init 0 */

  /* USER CODE END SDMMC1_Init 0 */

  /* USER CODE BEGIN SDMMC1_Init 1 */

  /* USER CODE END SDMMC1_Init 1 */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockBypass = SDMMC_CLOCK_BYPASS_DISABLE;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_1B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  hsd1.Init.ClockDiv = 0;
  /* USER CODE BEGIN SDMMC1_Init 2 */
  /* USER CODE END SDMMC1_Init 2 */
}

/**
 * @brief TIM1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 16 - 1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 100 - 1;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);
}

/**
 * @brief TIM7 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM7_Init(void)
{

  /* USER CODE BEGIN TIM7_Init 0 */

  /* USER CODE END TIM7_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM7_Init 1 */

  /* USER CODE END TIM7_Init 1 */
  htim7.Instance = TIM7;
  htim7.Init.Prescaler = 0;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim7.Init.Period = 1600;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim7) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim7, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM7_Init 2 */

  /* USER CODE END TIM7_Init 2 */
}

/**
 * @brief TIM15 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM15_Init(void)
{

  /* USER CODE BEGIN TIM15_Init 0 */

  /* USER CODE END TIM15_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM15_Init 1 */

  /* USER CODE END TIM15_Init 1 */
  htim15.Instance = TIM15;
  htim15.Init.Prescaler = 16 - 1;
  htim15.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim15.Init.Period = 100 - 1;
  htim15.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim15.Init.RepetitionCounter = 0;
  htim15.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim15) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim15, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim15) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim15, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim15, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim15, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM15_Init 2 */

  /* USER CODE END TIM15_Init 2 */
  HAL_TIM_MspPostInit(&htim15);
}

/**
 * @brief USART1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 19200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */
  /* Deinit the peripheral, gets inited by the cells */
  if (HAL_UART_DeInit(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END USART1_Init 2 */
}

/**
 * @brief USART2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 19200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
  huart2.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_ENABLE;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */
  /* Deinit the peripheral, gets inited by the cells */
  if (HAL_UART_DeInit(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END USART2_Init 2 */
}

/**
 * @brief USART3 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 19200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
  huart3.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_ENABLE;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */
  /* Deinit the peripheral, gets inited by the cells */
  if (HAL_UART_DeInit(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END USART3_Init 2 */
}

/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Channel4_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel4_IRQn);
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, CAN_SHDN_Pin | SOLENOID_Pin | GPIO_A_Pin | SOL_DIS_BATT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, BATTERY_EN_Pin | BUS_SEL2_Pin | BUS_SEL1_Pin | GPIO_B_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED0_Pin | LED1_Pin | LED2_Pin | LED3_Pin | LED4_Pin | LED5_Pin | LED6_Pin | LED7_Pin | SOL_DIS_CAN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : CAN_EN_Pin CAN_SILENT_Pin VER_DET_1_Pin VER_DET_2_Pin
                           VER_DET_3_Pin */
  GPIO_InitStruct.Pin = CAN_EN_Pin | CAN_SILENT_Pin | VER_DET_1_Pin | VER_DET_2_Pin | VER_DET_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : CAN_SHDN_Pin SOLENOID_Pin GPIO_A_Pin SOL_DIS_BATT_Pin */
  GPIO_InitStruct.Pin = CAN_SHDN_Pin | SOLENOID_Pin | GPIO_A_Pin | SOL_DIS_BATT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : BATTERY_EN_Pin BUS_SEL2_Pin BUS_SEL1_Pin GPIO_B_Pin */
  GPIO_InitStruct.Pin = BATTERY_EN_Pin | BUS_SEL2_Pin | BUS_SEL1_Pin | GPIO_B_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : VCC_STAT_Pin BUS_STAT_Pin */
  GPIO_InitStruct.Pin = VCC_STAT_Pin | BUS_STAT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LED0_Pin LED1_Pin LED2_Pin LED3_Pin
                           LED4_Pin LED5_Pin LED6_Pin LED7_Pin
                           SOL_DIS_CAN_Pin */
  GPIO_InitStruct.Pin = LED0_Pin | LED1_Pin | LED2_Pin | LED3_Pin | LED4_Pin | LED5_Pin | LED6_Pin | LED7_Pin | SOL_DIS_CAN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_DET_Pin */
  GPIO_InitStruct.Pin = SD_DET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(SD_DET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ADC1_ALERT_Pin ADC2_ALERT_Pin */
  GPIO_InitStruct.Pin = ADC1_ALERT_Pin | ADC2_ALERT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SOL_STAT_Pin */
  GPIO_InitStruct.Pin = SOL_STAT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SOL_STAT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PH3 */
  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
typedef void (*BootloaderJumpFunc_t)(void);
void JumpToBootloader(void)
{
  (void)__enable_irq();
  (void)HAL_RCC_DeInit();
  (void)HAL_DeInit();
  SysTick->VAL = 0;
  SysTick->LOAD = 0;
  SysTick->CTRL = 0;
  (void)__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();

  const uint32_t p = (*((uint32_t *)0x1FFF0000));
  __set_MSP(p);

  /* Embedded crimes to jump into the bootloader*/
  /* 0x1FFF0004 is the start address of the system rom, ST bootloader*/
  /* Ref rm0394 page 75/76 */
  BootloaderJumpFunc_t SysMemBootJump = (void (*)(void))(*((uint32_t *)0x1FFF0004));
  SysMemBootJump();
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_WatchdogTask */
/**
 * @brief  Function implementing the watchdogTask thread.
 * @retval None
 */
/* USER CODE END Header_WatchdogTask */
void WatchdogTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  (void)argument;
  /* Infinite loop */
  for (;;)
  {
    (void)HAL_IWDG_Refresh(&hiwdg);
    (void)osDelay(pdMS_TO_TICKS(100));
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_SDInitTask */
/**
 * @brief Function implementing the sDInitTask thread.
 * @retval None
 */
/* USER CODE END Header_SDInitTask */
void SDInitTask(void *argument)
{
  /* USER CODE BEGIN SDInitTask */
  (void)argument;
  (void)osDelay(TIMEOUT_1S_TICKS);
  StartLogTask();
  (void)vTaskDelete(NULL);
  /* USER CODE END SDInitTask */
}

/**
 * @brief  Period elapsed callback in non blocking mode
 * @note   This function is called  when TIM6 interrupt took place, inside
 * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
 * a global variable "uwTick" used as application time base.
 * @param  htim : TIM handle
 * @retval None
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */
  (void)htim;
  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    (void)HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  NON_FATAL_ERROR(CRITICAL_ERR);
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(const uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  blocking_serial_printf("ASSERT failed %s: %d", file, line);
  FATAL_ERROR(ASSERT_FAIL);
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
