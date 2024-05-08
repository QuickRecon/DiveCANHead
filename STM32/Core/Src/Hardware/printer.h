#pragma once

#include "stdbool.h"

#ifdef __cplusplus
extern "C"
{
#endif
    void InitPrinter(bool uartOut);
    void serial_printf(const char *fmt, ...);
    void blocking_serial_printf(const char *fmt, ...);
#ifdef __cplusplus
}
#endif
