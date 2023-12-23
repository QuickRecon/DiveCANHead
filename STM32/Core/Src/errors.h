#ifndef __ERROR_H
#define __ERROR_H

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
    ERR_NONE,
    CRITICAL_ERROR,       // Generic error, RTOS error handler got triggered
    TIMEOUT_ERROR,        // What we were waiting for never came :(
    QUEUEING_ERROR,       // We weren't able to successfully lodge an element in the queue
    CAN_OVERFLOW,         // Inbound CAN message is longer than 8 bytes long
    UNDEFINED_CAL_METHOD, // The configured calibration method is not defined (config corrupt?)
    INVALID_CELL_NUMBER   // The cell number can't be mapped to an input (too high?)
} NonFatalError_t;

void NonFatalError(NonFatalError_t error);
void NonFatalErrorISR(NonFatalError_t error);
void FatalError(FatalError_t error);

#endif
