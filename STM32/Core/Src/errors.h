#ifndef __ERROR_H
#define __ERROR_H

#include "common.h"

static const uint32_t FLAG_ERR_MASK = 0xFFFFFFF0u;
typedef enum FatalError_e
{
    FATAL_ERR_NONE,
    STACK_OVERFLOW,
    MALLOC_FAIL,
    HARD_FAULT,
    NMI_TRIGGERED,
    MEM_FAULT,
    BUS_FAULT,
    UNDEFINED_STATE
} FatalError_t;

typedef enum NonFatalError_e
{
    ERR_NONE = 0,

    /// @brief We weren't able to lock/unlock the flash
    FLASH_LOCK_ERROR,

    /// @brief We weren't able to read from our eeprom emulation
    EEPROM_ERROR,

    /// @brief The data we are looking at is out of date, subtly different to timeout
    OUT_OF_DATE_ERROR,

    /// @brief We failed to undertake an I2C operation
    I2C_BUS_ERROR,

    /// @brief We failed to undertake an UART operation
    UART_ERROR,

    /// @brief Check code that should be unreachable, if we got triggered here then something strange happened
    UNREACHABLE_ERROR,

    /// @brief We had an error with the os flags, can be a timeout if the flag wasn't set in time
    FLAG_ERROR,

    /// @brief Generic error, RTOS error handler got triggered
    CRITICAL_ERROR,

    /// @brief What we were waiting for never came :(
    TIMEOUT_ERROR,

    /// @brief We weren't able to successfully lodge an element in the queue
    QUEUEING_ERROR,

    /// @brief Inbound CAN message is longer than 8 bytes long
    CAN_OVERFLOW,

    /// @brief We couldn't add the can message to the outbound buffer
    CAN_TX_ERR,

    /// @brief The configured calibration method is not defined (config corrupt?)
    UNDEFINED_CAL_METHOD,

    /// @brief The configured calibration method cannot complete
    CAL_METHOD_ERROR,

    /// @brief The cell number can't be mapped to an input (too high?)
    INVALID_CELL_NUMBER
} NonFatalError_t;

void NonFatalError_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName);
void NonFatalErrorISR_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName);
void NonFatalError(NonFatalError_t error, uint32_t lineNumber, const char *fileName);
void NonFatalErrorISR(NonFatalError_t error, uint32_t lineNumber, const char *fileName);

void FatalError(FatalError_t error, uint32_t lineNumber, const char *fileName);

// These are a bit criminal but they let me inject file and line info without having to bang out macros every time
#define NON_FATAL_ERROR(x) (NonFatalError(x, __LINE__, __FILE__))
#define NON_FATAL_ERROR_DETAIL(x, y) (NonFatalError_Detail(x, y, __LINE__, __FILE__))
#define NON_FATAL_ERROR_ISR(x) (NonFatalErrorISR(x, __LINE__, __FILE__))
#define NON_FATAL_ERROR_ISR_DETAIL(x, y) (NonFatalErrorISR_Detail(x, y, __LINE__, __FILE__))
#endif
