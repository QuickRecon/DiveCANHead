#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
    void InitPrinter(void);
    void serial_printf(const char *fmt, ...);
    void blocking_serial_printf(const char *fmt, ...);
#ifdef __cplusplus
}
#endif
