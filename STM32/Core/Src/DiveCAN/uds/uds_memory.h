/**
 * @file uds_memory.h
 * @brief UDS Memory Upload/Download services
 *
 * Implements memory region abstraction for UDS services:
 * - 0x35: RequestUpload - Initiate memory read
 * - 0x36: TransferData - Transfer memory blocks
 * - 0x37: RequestTransferExit - Complete transfer
 * - 0x34: RequestDownload - Initiate firmware write (Phase 6)
 *
 * Memory Region Mapping (UDS Address → Physical):
 * - BLOCK1 (0xC2000080-0xC2000FFF): Flash page 0x00000080 (config, 8-byte aligned)
 * - BLOCK2 (0xC3001000-0xC3FFFFFF): Flash page 0x00010000 (logs, 4KB aligned)
 * - BLOCK3 (0xC5000000-0xC500007F): MCU unique ID at 0x1FFFF7F0 (read-only)
 *
 * @note Upload requires Extended Diagnostic session
 * @note Download requires Programming session + authentication (Phase 6)
 */

#ifndef UDS_MEMORY_H
#define UDS_MEMORY_H

#include <stdint.h>
#include <stdbool.h>

// Memory region identifiers
typedef enum {
    MEMORY_REGION_INVALID = 0,
    MEMORY_REGION_BLOCK1,   ///< Flash config area (0xC2000080-0xC2000FFF)
    MEMORY_REGION_BLOCK2,   ///< Flash log area (0xC3001000-0xC3FFFFFF)
    MEMORY_REGION_BLOCK3    ///< MCU unique ID (0xC5000000-0xC500007F)
} UDS_MemoryRegion_t;

// Memory region definition
typedef struct {
    uint32_t udsAddressStart;   ///< UDS address space start
    uint32_t udsAddressEnd;     ///< UDS address space end (inclusive)
    uint32_t physicalAddress;   ///< Physical address offset
    bool uploadAllowed;         ///< Can read via RequestUpload
    bool downloadAllowed;       ///< Can write via RequestDownload
    uint16_t alignment;         ///< Address alignment requirement (bytes)
} MemoryRegionDef_t;

// Upload/Download transfer state
typedef struct {
    bool active;                ///< Transfer in progress
    bool isUpload;              ///< True = upload (read), false = download (write)
    UDS_MemoryRegion_t region;      ///< Active memory region
    uint32_t address;           ///< Current UDS address
    uint32_t bytesRemaining;    ///< Bytes remaining in transfer
    uint8_t sequenceCounter;    ///< TransferData sequence (1-255, wraps)
    uint16_t maxBlockLength;    ///< Maximum bytes per TransferData frame
} MemoryTransferState_t;

// Maximum transfer block size (ISO-TP payload minus service overhead)
// TransferData format: [0x36, sequenceCounter, ...data]
// Max ISO-TP payload = 128 bytes, so max data = 128 - 2 = 126 bytes
#define MEMORY_MAX_BLOCK_LENGTH 126

/**
 * @brief Initialize memory transfer state
 *
 * @param state Transfer state to initialize
 */
void UDS_Memory_Init(MemoryTransferState_t *state);

/**
 * @brief Validate UDS address and resolve to memory region
 *
 * @param udsAddress UDS address to validate
 * @param length Number of bytes to access
 * @param isUpload True for upload (read), false for download (write)
 * @return Memory region, or MEMORY_REGION_INVALID if invalid
 */
UDS_MemoryRegion_t UDS_Memory_ValidateAddress(uint32_t udsAddress, uint32_t length, bool isUpload);

/**
 * @brief Start memory upload transfer
 *
 * Called from RequestUpload (0x35) service handler.
 *
 * @param state Transfer state
 * @param udsAddress Starting UDS address
 * @param length Number of bytes to upload
 * @param maxBlockLength Maximum bytes per block (output parameter)
 * @return true if upload started successfully, false if invalid
 */
bool UDS_Memory_StartUpload(MemoryTransferState_t *state, uint32_t udsAddress, uint32_t length, uint16_t *maxBlockLength);

/**
 * @brief Start memory download transfer
 *
 * Called from RequestDownload (0x34) service handler.
 *
 * @param state Transfer state
 * @param udsAddress Starting UDS address
 * @param length Number of bytes to download
 * @param maxBlockLength Maximum bytes per block (output parameter)
 * @return true if download started successfully, false if invalid
 */
bool UDS_Memory_StartDownload(MemoryTransferState_t *state, uint32_t udsAddress, uint32_t length, uint16_t *maxBlockLength);

/**
 * @brief Read memory block for TransferData
 *
 * Called from TransferData (0x36) service handler during upload.
 *
 * @param state Transfer state
 * @param sequenceCounter Expected sequence counter (1-255)
 * @param buffer Output buffer for data
 * @param bufferSize Size of output buffer
 * @param bytesRead Number of bytes read (output parameter)
 * @return true if read successful, false if error (sequence mismatch, etc.)
 */
bool UDS_Memory_ReadBlock(MemoryTransferState_t *state, uint8_t sequenceCounter, uint8_t *buffer, uint16_t bufferSize, uint16_t *bytesRead);

/**
 * @brief Write memory block for TransferData
 *
 * Called from TransferData (0x36) service handler during download.
 *
 * @param state Transfer state
 * @param sequenceCounter Expected sequence counter (1-255)
 * @param buffer Input buffer with data to write
 * @param dataLength Length of data to write
 * @return true if write successful, false if error (sequence mismatch, flash write error, etc.)
 */
bool UDS_Memory_WriteBlock(MemoryTransferState_t *state, uint8_t sequenceCounter, const uint8_t *buffer, uint16_t dataLength);

/**
 * @brief Complete memory transfer
 *
 * Called from RequestTransferExit (0x37) service handler.
 *
 * @param state Transfer state
 * @return true if transfer completed successfully
 */
bool UDS_Memory_CompleteTransfer(MemoryTransferState_t *state);

/**
 * @brief Read from physical flash memory
 *
 * Internal helper to read from flash region.
 *
 * @param physicalAddress Physical flash address
 * @param buffer Output buffer
 * @param length Number of bytes to read
 * @return true if read successful
 */
bool UDS_Memory_ReadFlash(uint32_t physicalAddress, uint8_t *buffer, uint16_t length);

/**
 * @brief Read from MCU unique ID register
 *
 * Internal helper to read MCU ID (96 bits at 0x1FFF7590).
 *
 * @param offset Offset within MCU ID region (0-11)
 * @param buffer Output buffer
 * @param length Number of bytes to read
 * @return true if read successful
 */
bool UDS_Memory_ReadMCUID(uint32_t offset, uint8_t *buffer, uint16_t length);

/**
 * @brief Write to physical flash memory
 *
 * Internal helper to write to flash region.
 * Handles flash unlock, erase (if needed), program, and lock.
 *
 * @param physicalAddress Physical flash address (must be 8-byte aligned)
 * @param buffer Data to write
 * @param length Number of bytes to write (must be multiple of 8)
 * @return true if write successful
 */
bool UDS_Memory_WriteFlash(uint32_t physicalAddress, const uint8_t *buffer, uint16_t length);

#endif // UDS_MEMORY_H
