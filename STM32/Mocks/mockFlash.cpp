/**
 * @file mockFlash.cpp
 * @brief Mock flash memory for testing UDS memory upload/download
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "CppUTestExt/MockSupport.h"

// Simulated flash memory (256KB)
static uint8_t mockFlashMemory[256 * 1024];

extern "C" {

/**
 * @brief Mock implementation of UDS_Memory_ReadFlash
 * Reads from simulated flash instead of real memory-mapped flash
 */
bool UDS_Memory_ReadFlash(uint32_t physicalAddress, uint8_t *buffer, uint16_t length)
{
    const uint32_t FLASH_BASE = 0x08000000;
    const uint32_t FLASH_SIZE = 256 * 1024;

    if (buffer == NULL || length == 0)
    {
        return false;
    }

    if (physicalAddress < FLASH_BASE || (physicalAddress + length) > (FLASH_BASE + FLASH_SIZE))
    {
        return false;
    }

    // Calculate offset from flash base
    uint32_t offset = physicalAddress - FLASH_BASE;

    // Read from simulated flash
    memcpy(buffer, &mockFlashMemory[offset], length);

    return true;
}

/**
 * @brief Mock implementation of UDS_Memory_ReadMCUID
 * Returns a fake MCU ID for testing
 */
bool UDS_Memory_ReadMCUID(uint32_t offset, uint8_t *buffer, uint16_t length)
{
    const uint8_t fakeMCUID[12] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC};

    if (buffer == NULL || length == 0)
    {
        return false;
    }

    if (offset >= 12 || (offset + length) > 12)
    {
        return false;
    }

    memcpy(buffer, &fakeMCUID[offset], length);

    return true;
}

/**
 * @brief Mock implementation of UDS_Memory_WriteFlash
 * Allows tests to control write success/failure via CppUTest mocks
 */
bool UDS_Memory_WriteFlash(uint32_t physicalAddress, const uint8_t *buffer, uint16_t length)
{
    // Use CppUTest mock to allow tests to control behavior
    bool result = mock().actualCall("UDS_Memory_WriteFlash")
        .withParameter("physicalAddress", physicalAddress)
        .withParameter("length", length)
        .returnBoolValueOrDefault(true);

    if (result)
    {
        // If mock says write succeeds, actually write to mock flash
        const uint32_t FLASH_BASE = 0x08000000;
        const uint32_t FLASH_SIZE = 256 * 1024;

        if (buffer != NULL && length > 0 &&
            physicalAddress >= FLASH_BASE &&
            (physicalAddress + length) <= (FLASH_BASE + FLASH_SIZE))
        {
            uint32_t offset = physicalAddress - FLASH_BASE;
            memcpy(&mockFlashMemory[offset], buffer, length);
        }
    }

    return result;
}

} // extern "C"
