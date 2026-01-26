/**
 * @file uds_memory.c
 * @brief UDS Memory Upload/Download implementation
 *
 * Provides memory region abstraction and block transfer for UDS services.
 */

#include "uds_memory.h"
#include "../../errors.h"
#include "../../Hardware/flash.h"
#include <string.h>
#include <stdint.h>

// STM32L4 MCU Unique ID address (96 bits at address 0x1FFF7590)
#define MCU_ID_ADDRESS 0x1FFF7590
#define MCU_ID_LENGTH 12

// Memory region definitions
// Note: FLASH_BASE is defined in STM32 headers as 0x08000000
static const MemoryRegionDef_t memoryRegions[] = {
    // BLOCK1: Flash configuration area (flash address 0x08000080-0x08000FFF)
    {
        .udsAddressStart = 0xC2000080,
        .udsAddressEnd = 0xC2000FFF,
        .physicalAddress = 0x08000080, // Absolute flash address
        .uploadAllowed = true,
        .downloadAllowed = false, // Phase 6: enable for firmware download
        .alignment = 8},
    // BLOCK2: Flash log area (flash address 0x08010000-...)
    {
        .udsAddressStart = 0xC3001000,
        .udsAddressEnd = 0xC3FFFFFF,
        .physicalAddress = 0x08010000, // Absolute flash address
        .uploadAllowed = true,
        .downloadAllowed = true, // Phase 6: enabled for firmware download
        .alignment = 4096},
    // BLOCK3: MCU Unique ID (read-only)
    {
        .udsAddressStart = 0xC5000000,
        .udsAddressEnd = 0xC500007F,
        .physicalAddress = MCU_ID_ADDRESS,
        .uploadAllowed = true,
        .downloadAllowed = false, // MCU ID is read-only
        .alignment = 1}};

#define MEMORY_REGION_COUNT (sizeof(memoryRegions) / sizeof(memoryRegions[0]))

/**
 * @brief Initialize memory transfer state
 */
void UDS_Memory_Init(MemoryTransferState_t *state)
{
    if (state == NULL)
    {
        return;
    }

    memset(state, 0, sizeof(MemoryTransferState_t));
    state->active = false;
    state->region = MEMORY_REGION_INVALID;
}

/**
 * @brief Validate UDS address and resolve to memory region
 */
UDS_MemoryRegion_t UDS_Memory_ValidateAddress(uint32_t udsAddress, uint32_t length, bool isUpload)
{
    // Check each memory region
    for (uint8_t i = 0; i < MEMORY_REGION_COUNT; i++)
    {
        const MemoryRegionDef_t *region = &memoryRegions[i];

        // Check if address is within region
        if (udsAddress >= region->udsAddressStart && udsAddress <= region->udsAddressEnd)
        {
            // Check if access would exceed region bounds
            if ((udsAddress + length - 1) > region->udsAddressEnd)
            {
                return MEMORY_REGION_INVALID; // Would exceed region
            }

            // Check access permissions
            if (isUpload && !region->uploadAllowed)
            {
                return MEMORY_REGION_INVALID; // Upload not allowed
            }
            if (!isUpload && !region->downloadAllowed)
            {
                return MEMORY_REGION_INVALID; // Download not allowed
            }

            // Return region ID (1-based index)
            return (UDS_MemoryRegion_t)(i + 1);
        }
    }

    return MEMORY_REGION_INVALID; // Address not in any region
}

/**
 * @brief Start memory upload transfer
 */
bool UDS_Memory_StartUpload(MemoryTransferState_t *state, uint32_t udsAddress, uint32_t length, uint16_t *maxBlockLength)
{
    if (state == NULL || maxBlockLength == NULL)
    {
        return false;
    }

    // Validate address and get region
    UDS_MemoryRegion_t region = UDS_Memory_ValidateAddress(udsAddress, length, true);
    if (region == MEMORY_REGION_INVALID)
    {
        return false; // Invalid address or region
    }

    // Initialize transfer state
    state->active = true;
    state->isUpload = true;
    state->region = region;
    state->address = udsAddress;
    state->bytesRemaining = length;
    state->sequenceCounter = 1; // Sequence starts at 1
    state->maxBlockLength = MEMORY_MAX_BLOCK_LENGTH;

    *maxBlockLength = state->maxBlockLength;

    return true;
}

/**
 * @brief Start memory download transfer
 */
bool UDS_Memory_StartDownload(MemoryTransferState_t *state, uint32_t udsAddress, uint32_t length, uint16_t *maxBlockLength)
{
    if (state == NULL || maxBlockLength == NULL)
    {
        return false;
    }

    // Validate address and get region (isUpload=false for download)
    UDS_MemoryRegion_t region = UDS_Memory_ValidateAddress(udsAddress, length, false);
    if (region == MEMORY_REGION_INVALID)
    {
        return false; // Invalid address or region
    }

    // Initialize transfer state
    state->active = true;
    state->isUpload = false; // Download (write)
    state->region = region;
    state->address = udsAddress;
    state->bytesRemaining = length;
    state->sequenceCounter = 1; // Sequence starts at 1
    state->maxBlockLength = MEMORY_MAX_BLOCK_LENGTH;

    *maxBlockLength = state->maxBlockLength;

    return true;
}

/**
 * @brief Read memory block for TransferData
 */
bool UDS_Memory_ReadBlock(MemoryTransferState_t *state, uint8_t sequenceCounter, uint8_t *buffer, uint16_t bufferSize, uint16_t *bytesRead)
{
    if (state == NULL || buffer == NULL || bytesRead == NULL)
    {
        return false;
    }

    // Validate transfer is active and is upload
    if (!state->active || !state->isUpload)
    {
        return false;
    }

    // Validate sequence counter
    if (sequenceCounter != state->sequenceCounter)
    {
        return false; // Sequence mismatch
    }

    // Calculate how many bytes to read (min of maxBlockLength, bytesRemaining, bufferSize)
    uint16_t toRead = state->maxBlockLength;
    if (toRead > state->bytesRemaining)
    {
        toRead = state->bytesRemaining;
    }
    if (toRead > bufferSize)
    {
        toRead = bufferSize;
    }

    // Get memory region definition
    const MemoryRegionDef_t *regionDef = &memoryRegions[state->region - 1];

    // Calculate physical address
    uint32_t physicalAddress = regionDef->physicalAddress + (state->address - regionDef->udsAddressStart);

    // Read from appropriate memory type
    bool success = false;
    if (state->region == MEMORY_REGION_BLOCK3)
    {
        // Read from MCU ID
        uint32_t offset = state->address - regionDef->udsAddressStart;
        success = UDS_Memory_ReadMCUID(offset, buffer, toRead);
    }
    else
    {
        // Read from flash
        success = UDS_Memory_ReadFlash(physicalAddress, buffer, toRead);
    }

    if (!success)
    {
        return false;
    }

    // Update transfer state
    state->address += toRead;
    state->bytesRemaining -= toRead;
    state->sequenceCounter++;
    if (state->sequenceCounter == 0)
    {
        state->sequenceCounter = 1; // Wrap at 256 (skip 0)
    }

    *bytesRead = toRead;

    // Note: Transfer remains active until RequestTransferExit is called,
    // even if all bytes have been transferred
    return true;
}

/**
 * @brief Write memory block for TransferData
 */
bool UDS_Memory_WriteBlock(MemoryTransferState_t *state, uint8_t sequenceCounter, const uint8_t *buffer, uint16_t dataLength)
{
    if (state == NULL || buffer == NULL)
    {
        return false;
    }

    // Validate transfer is active and is download
    if (!state->active || state->isUpload)
    {
        return false;
    }

    // Validate sequence counter
    if (sequenceCounter != state->sequenceCounter)
    {
        return false; // Sequence mismatch
    }

    // Validate data length doesn't exceed remaining bytes
    if (dataLength > state->bytesRemaining)
    {
        return false; // Too much data
    }

    // Get memory region definition
    const MemoryRegionDef_t *regionDef = &memoryRegions[state->region - 1];

    // Calculate physical address
    uint32_t physicalAddress = regionDef->physicalAddress + (state->address - regionDef->udsAddressStart);

    // Write to flash
    bool success = UDS_Memory_WriteFlash(physicalAddress, buffer, dataLength);

    if (!success)
    {
        return false;
    }

    // Update transfer state
    state->address += dataLength;
    state->bytesRemaining -= dataLength;
    state->sequenceCounter++;
    if (state->sequenceCounter == 0)
    {
        state->sequenceCounter = 1; // Wrap at 256 (skip 0)
    }

    // Note: Transfer remains active until RequestTransferExit is called,
    // even if all bytes have been transferred
    return true;
}

/**
 * @brief Complete memory transfer
 */
bool UDS_Memory_CompleteTransfer(MemoryTransferState_t *state)
{
    if (state == NULL)
    {
        return false;
    }

    if (!state->active)
    {
        return false; // No active transfer
    }

    // Verify all bytes transferred
    if (state->bytesRemaining != 0)
    {
        // Transfer incomplete
        state->active = false;
        return false;
    }

    // Mark transfer inactive
    state->active = false;

    return true;
}

#ifndef TESTING
/**
 * @brief Read from physical flash memory
 */
bool UDS_Memory_ReadFlash(uint32_t physicalAddress, uint8_t *buffer, uint16_t length)
{
    if (buffer == NULL || length == 0)
    {
        return false;
    }

    // Validate address is within STM32L4 flash range (0x08000000-0x0803FFFF = 256KB)
    // FLASH_BASE is defined in STM32 headers as 0x08000000
    const uint32_t flashSize = 256 * 1024; // 256KB

    if (physicalAddress < FLASH_BASE || (physicalAddress + length) > (FLASH_BASE + flashSize))
    {
        return false; // Out of flash range
    }

    // Read directly from flash memory (memory-mapped)
    const uint8_t *flashPtr = (const uint8_t *)(uintptr_t)physicalAddress;
    memcpy(buffer, flashPtr, length);

    return true;
}

/**
 * @brief Read from MCU unique ID register
 */
bool UDS_Memory_ReadMCUID(uint32_t offset, uint8_t *buffer, uint16_t length)
{
    if (buffer == NULL || length == 0)
    {
        return false;
    }

    // Validate offset and length
    if (offset >= MCU_ID_LENGTH || (offset + length) > MCU_ID_LENGTH)
    {
        return false; // Out of MCU ID range
    }

    // Read from MCU ID address (memory-mapped)
    const uint8_t *mcuIdPtr = (const uint8_t *)(uintptr_t)(MCU_ID_ADDRESS + offset);
    memcpy(buffer, mcuIdPtr, length);

    return true;
}

/**
 * @brief Write to physical flash memory
 *
 * IMPORTANT: This is a placeholder for Phase 6. Real implementation will:
 * 1. Unlock flash
 * 2. Erase pages if needed
 * 3. Program flash (8-byte aligned double-words)
 * 4. Lock flash
 * 5. Verify write
 */
bool UDS_Memory_WriteFlash(uint32_t physicalAddress, const uint8_t *buffer, uint16_t length)
{
    if (buffer == NULL || length == 0)
    {
        return false;
    }

    // Validate address is within STM32L4 flash range (0x08000000-0x0803FFFF = 256KB)
    const uint32_t flashSize = 256 * 1024; // 256KB

    if (physicalAddress < FLASH_BASE || (physicalAddress + length) > (FLASH_BASE + flashSize))
    {
        return false; // Out of flash range
    }

    // TODO: Implement flash programming using STM32 HAL
    // For now, this is a stub that will be mocked in tests
    // Real implementation in Phase 6 final step

    return true; // Placeholder - always succeeds
}
#endif // TESTING
