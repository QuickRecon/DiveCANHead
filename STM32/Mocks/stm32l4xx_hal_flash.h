#pragma once
#include "stm32l4xx_hal_def.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define OB_USER_BOR_LEV ((uint32_t)0x0001)    /*!< BOR reset Level */
#define OB_USER_nRST_STOP ((uint32_t)0x0002)  /*!< Reset generated when entering the stop mode */
#define OB_USER_nRST_STDBY ((uint32_t)0x0004) /*!< Reset generated when entering the standby mode */
#define OB_USER_IWDG_SW ((uint32_t)0x0008)    /*!< Independent watchdog selection */
#define OB_USER_IWDG_STOP ((uint32_t)0x0010)  /*!< Independent watchdog counter freeze in stop mode */
#define OB_USER_IWDG_STDBY ((uint32_t)0x0020) /*!< Independent watchdog counter freeze in standby mode */
#define OB_USER_WWDG_SW ((uint32_t)0x0040)    /*!< Window watchdog selection */
#define OB_USER_BFB2 ((uint32_t)0x0080)       /*!< Dual-bank boot */
#define OB_USER_DUALBANK ((uint32_t)0x0100)   /*!< Dual-Bank on 1MB or 512kB Flash memory devices */
#define OB_USER_nBOOT1 ((uint32_t)0x0200)     /*!< Boot configuration */
#define OB_USER_SRAM2_PE ((uint32_t)0x0400)   /*!< SRAM2 parity check enable */
#define OB_USER_SRAM2_RST ((uint32_t)0x0800)  /*!< SRAM2 Erase when system reset */
#define OB_USER_nRST_SHDW ((uint32_t)0x1000)  /*!< Reset generated when entering the shutdown mode */
#define OB_USER_nSWBOOT0 ((uint32_t)0x2000)   /*!< Software BOOT0 */
#define OB_USER_nBOOT0 ((uint32_t)0x4000)     /*!< nBOOT0 option bit */
#define OB_USER_DBANK ((uint32_t)0x8000)      /*!< Single bank with 128-bits data or two banks with 64-bits data */

    typedef struct
    {
        uint32_t OptionType;     /*!< Option byte to be configured.
                                      This parameter can be a combination of the values of @ref FLASH_OB_Type */
        uint32_t WRPArea;        /*!< Write protection area to be programmed (used for OPTIONBYTE_WRP).
                                      Only one WRP area could be programmed at the same time.
                                      This parameter can be value of @ref FLASH_OB_WRP_Area */
        uint32_t WRPStartOffset; /*!< Write protection start offset (used for OPTIONBYTE_WRP).
                                      This parameter must be a value between 0 and (max number of pages in the bank - 1)
                                      (eg : 25 for 1MB dual bank) */
        uint32_t WRPEndOffset;   /*!< Write protection end offset (used for OPTIONBYTE_WRP).
                                      This parameter must be a value between WRPStartOffset and (max number of pages in the bank - 1) */
        uint32_t RDPLevel;       /*!< Set the read protection level.. (used for OPTIONBYTE_RDP).
                                      This parameter can be a value of @ref FLASH_OB_Read_Protection */
        uint32_t USERType;       /*!< User option byte(s) to be configured (used for OPTIONBYTE_USER).
                                      This parameter can be a combination of @ref FLASH_OB_USER_Type */
        uint32_t USERConfig;     /*!< Value of the user option byte (used for OPTIONBYTE_USER).
                                      This parameter can be a combination of @ref FLASH_OB_USER_BOR_LEVEL,
                                      @ref FLASH_OB_USER_nRST_STOP, @ref FLASH_OB_USER_nRST_STANDBY,
                                      @ref FLASH_OB_USER_nRST_SHUTDOWN, @ref FLASH_OB_USER_IWDG_SW,
                                      @ref FLASH_OB_USER_IWDG_STOP, @ref FLASH_OB_USER_IWDG_STANDBY,
                                      @ref FLASH_OB_USER_WWDG_SW, @ref FLASH_OB_USER_BFB2,
                                      @ref FLASH_OB_USER_DUALBANK, @ref FLASH_OB_USER_nBOOT1,
                                      @ref FLASH_OB_USER_SRAM2_PE, @ref FLASH_OB_USER_SRAM2_RST,
                                      @ref FLASH_OB_USER_nSWBOOT0 and @ref FLASH_OB_USER_nBOOT0 */
        uint32_t PCROPConfig;    /*!< Configuration of the PCROP (used for OPTIONBYTE_PCROP).
                                      This parameter must be a combination of @ref FLASH_Banks (except FLASH_BANK_BOTH)
                                      and @ref FLASH_OB_PCROP_RDP */
        uint32_t PCROPStartAddr; /*!< PCROP Start address (used for OPTIONBYTE_PCROP).
                                      This parameter must be a value between begin and end of bank
                                      => Be careful of the bank swapping for the address */
        uint32_t PCROPEndAddr;   /*!< PCROP End address (used for OPTIONBYTE_PCROP).
                                      This parameter must be a value between PCROP Start address and end of bank */
    } FLASH_OBProgramInitTypeDef;

    void HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *pOBInit);
    HAL_StatusTypeDef HAL_FLASHEx_OBProgram(FLASH_OBProgramInitTypeDef *pOBInit);
#ifdef __cplusplus
}
#endif