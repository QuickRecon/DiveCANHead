/**
  SPI Generated Driver API interface File

  @Company
    Microchip Technology Inc.

  @File Name
    spi_interface.h

  @Summary
    This is the generated driver interface file for the SPI driver.

  @Description
    This interface file provides APIs for driver for SPI.
    The generated drivers are tested against the following:
        Compiler          :  XC8 v2.20
        MPLAB             :  MPLABX v5.40
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

#ifndef SPI_INTERFACE_H
#define SPI_INTERFACE_H

/**
 Section: Included Files
*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus

    extern "C" {

#endif

/**
 Section: Data Type Definitions
*/
        
/**
  SPI Driver function structure.

  @Summary
    Structure containing the function pointers of SPI driver.
 */
struct SPI_INTERFACE
{   
    void (*Initialize)(void);
    void (*Close)(void);
    bool (*Open)(uint8_t spiConfigIndex);
    void (*BufferExchange)(void *bufferData, size_t bufferSize);
    void (*BufferRead)(void *bufferData, size_t bufferSize);
    void (*BufferWrite)(void *bufferData, size_t bufferSize); 
    uint8_t (*ByteExchange)(uint8_t byteData);    
    uint8_t (*ByteRead)(void);
    void (*ByteWrite)(uint8_t byteData);
};

#ifdef __cplusplus

    }

#endif

#endif //SPI_INTERFACE_H
