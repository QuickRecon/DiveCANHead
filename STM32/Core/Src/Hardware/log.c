#include "printer.h"
#include "fatfs.h"
#include <string.h>

void InitLog(void)
{
    serial_printf("Starting SD\r\n");
    FRESULT res = FR_OK; /* FatFs function common result code */
    uint32_t byteswritten = 0;
    // uint32_t bytesread = 0;                       /* File write/read counts */
    uint8_t wtext[] = "STM32 FATFS works great!"; /* File write buffer */
    uint8_t rtext[_MAX_SS];                       /* File read buffer */
    if (f_mount(&SDFatFS, (TCHAR const *)SDPath, 0) != FR_OK)
    {
        Error_Handler();
        serial_printf("Mount error \r\n");
    }
    else
    {
        res = f_mkfs((TCHAR const *)SDPath, FM_ANY, 0, rtext, sizeof(rtext));
        if ( res != FR_OK)
        {
            Error_Handler();
            serial_printf("MKfs error %d\r\n", res);
        }
        else
        {
            /*  Open file for writing (Create) */
            if (f_open(&SDFile, "STM32.TXT", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
            {
                Error_Handler();
                serial_printf("fopen error1\r\n");
            }
            else
            {
                /*  Write to the text file */
                res = f_write(&SDFile, wtext, strlen((char *)wtext), (void *)&byteswritten);
                if ((byteswritten == 0) || (res != FR_OK))
                {
                    Error_Handler();
                    serial_printf("bytes error1\r\n");
                }
                else
                {

                    f_close(&SDFile);
                }
            }
        }
    }
    f_mount(&SDFatFS, (TCHAR const *)NULL, 0);

    serial_printf("file written\r\n");
}
