#pragma once
#include "stm32l4xx_hal_def.h"

#ifdef __cplusplus
extern "C"
{
#endif
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