#include "errors.h"
#include "Hardware/printer.h"
#include "Hardware/flash.h"

void NonFatalError_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName)
{
    serial_printf("ERR CODE %d(0x%x) AT %s:%d\r\n", error, additionalInfo, fileName, lineNumber);
}

void NonFatalErrorISR_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName)
{
    blocking_serial_printf("ERR CODE %d(0x%x) AT %s:%d\r\n", error, additionalInfo, fileName, lineNumber);
}

void NonFatalError(NonFatalError_t error, uint32_t lineNumber, const char *fileName)
{
    NonFatalError_Detail(error, 0, lineNumber, fileName);
}

void NonFatalErrorISR(NonFatalError_t error, uint32_t lineNumber, const char *fileName)
{
    NonFatalErrorISR_Detail(error, 0, lineNumber, fileName);
}

void FatalError(FatalError_t error, uint32_t lineNumber, const char *fileName)
{
    blocking_serial_printf("!! FATAL ERR CODE %d AT %s:%d\r\n", error, fileName, lineNumber);
    (void)SetFatalError(error);
}
