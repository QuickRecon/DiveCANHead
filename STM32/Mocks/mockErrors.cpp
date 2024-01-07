#include "errors.h"
#include <stdio.h>

#include "CppUTestExt/MockSupport.h"

#include "../Hardware/printer.h"

static void PrintERR(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName)
{
    printf("ERR CODE %d(0x%x) AT %s:%d\r\n", error, additionalInfo, fileName, lineNumber);
}

void cppNonFatalError_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName)
{
    mock().actualCall("NonFatalError_Detail").withParameter("error", error);
    PrintERR(error, additionalInfo, lineNumber, fileName);
}
void cppNonFatalErrorISR_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName)
{
    mock().actualCall("NonFatalErrorISR_Detail").withParameter("error", error);
    PrintERR(error, additionalInfo, lineNumber, fileName);
}

void cppNonFatalError(NonFatalError_t error, uint32_t lineNumber, const char *fileName)
{
    mock().actualCall("NonFatalError").withParameter("error", error);
    PrintERR(error, 0, lineNumber, fileName);
}

void cppNonFatalErrorISR(NonFatalError_t error, uint32_t lineNumber, const char *fileName)
{
    mock().actualCall("NonFatalErrorISR").withParameter("error", error);
    PrintERR(error, 0, lineNumber, fileName);
}

void cppFatalError(FatalError_t error, uint32_t lineNumber, const char *fileName)
{
    mock().actualCall("FatalError").withParameter("error", error);
    printf("!! FATAL ERR CODE %d AT %s:%d\r\n", error, fileName, lineNumber);
}

extern "C"
{
    void NonFatalError_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName)
    {
        cppNonFatalError_Detail(error, additionalInfo, lineNumber, fileName);
    }
    void NonFatalErrorISR_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName)
    {
        cppNonFatalErrorISR_Detail(error, additionalInfo, lineNumber, fileName);
    }

    void NonFatalError(NonFatalError_t error, uint32_t lineNumber, const char *fileName)
    {
        cppNonFatalError(error, lineNumber, fileName);
    }

    void NonFatalErrorISR(NonFatalError_t error, uint32_t lineNumber, const char *fileName)
    {
        cppNonFatalErrorISR(error, lineNumber, fileName);
    }

    void FatalError(FatalError_t error, uint32_t lineNumber, const char *fileName)
    {
        cppFatalError(error, lineNumber, fileName);
    }
}
