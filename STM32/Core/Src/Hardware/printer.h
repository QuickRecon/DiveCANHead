#ifndef __PRINTER_H__
#define __PRINTER_H__

#ifdef __cplusplus
extern "C"
{
#endif
    void InitPrinter(void);
    void serial_printf(const char *fmt, ...);
#ifdef __cplusplus
}
#endif
#endif
