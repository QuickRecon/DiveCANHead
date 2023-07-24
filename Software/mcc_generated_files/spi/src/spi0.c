/**
  * SPI0 Generated Driver File
  *
  * @file spi0.c
  *
  * @ingroup spi0
  *
  * @brief Contains the API Implementations for SPI0 module.
  *
  * @version SPI0 Driver Version 2.0.1
*/
/*
� [2023] Microchip Technology Inc. and its subsidiaries.

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

#include "../spi0.h"

typedef struct spi0_descriptor_s 
{
    spi0_transfer_status_t status;
} spi0_descriptor_t;

/**
  SPI0_DRIVER_FUNCTION object 

  @brief Defines an object for SPI_DRIVER_FUNCTIONS.
 */
const struct SPI_INTERFACE SPI0_s = 
{
    .Initialize = SPI0_Initialize,
    .Close = SPI0_Close,
    .Open = SPI0_Open,
    .BufferExchange = SPI0_BufferExchange,
    .BufferRead = SPI0_BufferRead,
    .BufferWrite = SPI0_BufferWrite,	
    .ByteExchange = SPI0_ByteExchange,
    .ByteRead = SPI0_ByteRead,	
    .ByteWrite = SPI0_ByteWrite,
};

SPI0_configuration_t spi0_configurations[] =
{
    { 0x21, 0x00 }
};

static spi0_descriptor_t spi0_desc;

void SPI0_Initialize(void)
{
    //BUFEN enabled; BUFWR disabled; MODE 0; SSD disabled; 
    SPI0.CTRLB = 0x00;

    //CLK2X enabled; DORD disabled; ENABLE enabled; MASTER enabled; PRESC DIV4; 
    SPI0.CTRLA = 0x2123;

    //DREIE disabled; IE disabled; RXCIE disabled; SSIE disabled; TXCIE disabled; 
    SPI0.INTCTRL = 0x0;

    spi0_desc.status = SPI0_FREE;

    //BUFOVF disabled; DREIF disabled; RXCIF disabled; SSIF disabled; TXCIF disabled; IF disabled; WRCOL disabled; 
    SPI0.INTFLAGS = 0x0;
}

void SPI0_Enable(void)
{
    SPI0.CTRLA |= SPI_ENABLE_bm;
}

void SPI0_Disable(void)
{
    SPI0.CTRLA &= ~SPI_ENABLE_bm;
}

bool SPI0_Open(uint8_t spiConfigIndex)
{
    if (spi0_desc.status == SPI0_FREE) {
        spi0_desc.status = SPI0_IDLE;
        SPI0.CTRLB                = spi0_configurations[spiConfigIndex].CTRLBvalue;
        SPI0.CTRLA                = spi0_configurations[spiConfigIndex].CTRLAvalue;
        return true;
    } else {
        return false;
    }
}

void SPI0_Close(void)
{
    spi0_desc.status = SPI0_FREE;
}

uint8_t SPI0_ByteExchange(uint8_t data)
{
    SPI0.DATA = data;
    while (!(SPI0.INTFLAGS & SPI_RXCIF_bm));
    return SPI0.DATA;
}

bool SPI0_Selected(void)
{
/**
 * @brief Returns true if SS pin is selected. 
 * TODO: Place your code here
 */
return true;
}

void SPI0_WaitDataready(void)
{
    while (!(SPI0.INTFLAGS & SPI_RXCIF_bm))
        ;
}

void SPI0_BufferExchange(void *block, size_t size)
{
    uint8_t *b = (uint8_t *)block;
    while (size--) {
        SPI0.DATA = *b;
        while (!(SPI0.INTFLAGS & SPI_RXCIF_bm))
            ;
        *b = SPI0.DATA;
        b++;
    }
}

void SPI0_BufferWrite(void *block, size_t size)
{
    uint8_t *b = (uint8_t *)block;
    uint8_t  rdata;
    while (size--) {
        SPI0.DATA = *b;
        while (!(SPI0.INTFLAGS & SPI_RXCIF_bm))
            ;
        rdata = SPI0.DATA;
        (void)(rdata); // Silence compiler warning
        b++;
    }
}

void SPI0_BufferRead(void *block, size_t size)
{
    uint8_t *b = (uint8_t *)block;
    while (size--) {
        SPI0.DATA = 0;
        while (!(SPI0.INTFLAGS & SPI_RXCIF_bm))
            ;
        *b = SPI0.DATA;
        b++;
    }
}

void SPI0_ByteWrite(uint8_t data)
{
    SPI0.DATA = data;
}

uint8_t SPI0_ByteRead(void)
{
    return SPI0.DATA;
}

uint8_t SPI0_ExchangeByte(uint8_t data)
{
    return SPI0_ByteExchange(data);
}

void SPI0_ExchangeBlock(void *block, size_t blockSize)
{
    SPI0_BufferExchange(block, blockSize);
}

void SPI0_WriteBlock(void *block, size_t blockSize)
{
    SPI0_BufferWrite(block, blockSize);
}

void SPI0_ReadBlock(void *block, size_t blockSize)
{
    SPI0_BufferRead(block, blockSize);
}

void SPI0_WriteByte(uint8_t byte)
{
    SPI0_ByteWrite(byte);
}

uint8_t SPI0_ReadByte(void)
{
    return SPI0_ByteRead();
}
