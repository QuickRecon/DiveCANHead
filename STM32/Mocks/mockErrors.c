#include "errors.h"
#include <stdio.h>

extern void serial_printf(const char *fmt, ...);

void NonFatalError_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char* fileName)
{
    printf("ERR CODE %d(0x%x) AT %s:%d\r\n", error, additionalInfo, fileName, lineNumber);
}
void NonFatalErrorISR_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char* fileName)
{
    // In mock land ISR is the same thing as normal
    NonFatalError_Detail(error, additionalInfo, lineNumber, fileName);
}

void NonFatalError(NonFatalError_t error, uint32_t lineNumber, const char* fileName)
{
    NonFatalError_Detail(error, 0, lineNumber, fileName);
}

void NonFatalErrorISR(NonFatalError_t error, uint32_t lineNumber, const char* fileName)
{
    NonFatalErrorISR_Detail(error, 0, lineNumber, fileName);
}

void FatalError(FatalError_t error, uint32_t lineNumber, const char* fileName)
{
    printf("!! FATAL ERR CODE %d AT %s:%d\r\n", error, fileName, lineNumber);
}