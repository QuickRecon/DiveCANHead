/**
 *
 * @file nvm.c
 *
 * @ingroup nvm_driver
 *
 * @brief This file contains the implementation for NVM driver
 *
 * @version NVM Driver Version 2.1.0
 */
/*
© [2023] Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip 
    software and any derivatives exclusively with Microchip products. 
    You are responsible for complying with 3rd party license terms  
    applicable to your use of 3rd party software (including open source  
    software) that may accompany Microchip software. SOFTWARE IS ?AS IS.? 
    NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS 
    SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,  
    MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT 
    WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY 
    KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF 
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE 
    FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S 
    TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT 
    EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR 
    THIS SOFTWARE.
*/

#include <avr/pgmspace.h>
#include "../../system/utils/compiler.h"
#include "../../system/ccp.h"
#include "../nvm.h"


void NVM_Initialize(void)
{    
    //APPCODEWP disabled; APPDATAWP disabled; BOOTRP disabled; EEWP disabled; FLMAP SECTION0; FLMAPLOCK disabled; 
    ccp_write_io((void*)&NVMCTRL.CTRLB, 0x0);
}
nvm_status_t NVM_StatusGet(void)
{
    return (((NVMCTRL.STATUS & NVMCTRL_ERROR_gm) != 0) ? NVM_ERROR : NVM_OK);
}

void NVM_StatusClear(void)
{
    NVMCTRL.STATUS &= ~NVMCTRL_ERROR_gm;
}


flash_data_t FLASH_Read(flash_address_t address)
{
    return (*(flash_data_t *)(MAPPED_PROGMEM_START + (address & 0x7FFF)));
}

nvm_status_t FLASH_RowWrite(flash_address_t address, flash_data_t *dataBuffer)
{
    //Create a pointer in unified memory map to the page to write
    flash_data_t *data_space = (flash_data_t *) (MAPPED_PROGMEM_START + (address & 0x7FFF));
    uint8_t flashDataCount = PROGMEM_PAGE_SIZE;

    //Write data to the page buffer
    while (flashDataCount-- > 0U)
    {
        *data_space++ = *dataBuffer++;
    }

    // Write the flash page
    ccp_write_spm((void *)&NVMCTRL.CTRLA, NVMCTRL_CMD_FLPW_gc);
    return (((NVMCTRL.STATUS & NVMCTRL_ERROR_gm) != 0) ? NVM_ERROR : NVM_OK);
}

nvm_status_t FLASH_PageErase(flash_address_t flash_adr)
{
	// Create a pointer in unified memory map to the page to erase
	uint8_t *data_space = (uint8_t *)(MAPPED_PROGMEM_START + (flash_adr & 0x7FFF));

	// Perform a dummy write to this address to update the address register in NVMCTL
	*data_space = 0;

	// Erase the flash page
    ccp_write_spm((void *)&NVMCTRL.CTRLA, NVMCTRL_CMD_FLPER_gc);
    return (((NVMCTRL.STATUS & NVMCTRL_ERROR_gm) != 0) ? NVM_ERROR : NVM_OK);
}

bool FLASH_IsBusy(void)
{
    return (NVMCTRL.STATUS & NVMCTRL_FLBUSY_bm);
}

flash_address_t FLASH_PageAddressGet(flash_address_t address)
{
    return (flash_address_t) (address & ((PROGMEM_SIZE - 1) ^ (PROGMEM_PAGE_SIZE - 1)));
}

flash_address_t FLASH_PageOffsetGet(flash_address_t address)
{
    return (flash_address_t) (address & (PROGMEM_PAGE_SIZE - 1));
}


eeprom_data_t EEPROM_Read(eeprom_address_t address)
{	
	return *(eeprom_data_t *)(EEPROM_START + address);
	
}

nvm_status_t EEPROM_Write(eeprom_address_t address, eeprom_data_t data)
{
		// Write byte to page buffer 
		*(eeprom_data_t *)(EEPROM_START + address) = data;
		
		// Erase byte and program it with desired value 
		ccp_write_spm((void *)&NVMCTRL.CTRLA, NVMCTRL_CMD_EEPERW_gc); 
		return (((NVMCTRL.STATUS & NVMCTRL_ERROR_gm) != 0) ? NVM_ERROR : NVM_OK);
}

bool EEPROM_IsBusy(void)
{
    return (NVMCTRL.STATUS & NVMCTRL_EEBUSY_bm);
}



