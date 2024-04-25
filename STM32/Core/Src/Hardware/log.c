#include "printer.h"
#include "fatfs.h"
#include <string.h>

extern SD_HandleTypeDef hsd1;

/* SD card driver overrides to make the DMA work properly */
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks)
{
    uint8_t sd_state = MSD_OK;
    /* Since we are only using 1 DMA channel for SDIO
     * Change DMA direction before calling SD Read
     * Direction can only be changed when DMA is disabled */

    __HAL_DMA_DISABLE(hsd1.hdmatx);

    hsd1.hdmatx->Init.Direction = DMA_MEMORY_TO_PERIPH;

    hsd1.hdmatx->Instance->CCR |= DMA_CCR_DIR;

    /* Write block(s) in DMA transfer mode */
    if (HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)pData, WriteAddr, NumOfBlocks) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    return sd_state;
}

uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks)
{
    uint8_t sd_state = MSD_OK;

    /* Since we are only using 1 DMA channel for SDIO
     * Change DMA direction before calling SD Read
     * Direction can only be changed when DMA is disabled */

    __HAL_DMA_DISABLE(hsd1.hdmarx);

    hsd1.hdmarx->Init.Direction = DMA_PERIPH_TO_MEMORY;

    hsd1.hdmarx->Instance->CCR &= ~DMA_CCR_DIR;
    /* Read block(s) in DMA transfer mode */
    if (HAL_SD_ReadBlocks_DMA(&hsd1, (uint8_t *)pData, ReadAddr, NumOfBlocks) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    return sd_state;
}

void InitLog(void)
{
    serial_printf("Starting SD\r\n");
    FRESULT res = FR_OK; /* FatFs function common result code */
    uint32_t byteswritten = 0;
    uint8_t wtext[] = "STM32 FATFS works great!"; /* File write buffer */

    res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
    if (res != FR_OK)
    {
        Error_Handler();
        serial_printf("mount error %d\r\n", res);
    }
    else
    {
        /*  Open file for writing (Create) */
        res = f_open(&SDFile, "STM32.TXT", FA_CREATE_ALWAYS | FA_WRITE);
        if (res != FR_OK)
        {
            Error_Handler();
            serial_printf("fopen error %d\r\n", res);
        }
        else
        {
            /*  Write to the text file */
            res = f_write(&SDFile, wtext, strlen((char *)wtext), (void *)&byteswritten);
            if ((0 == byteswritten) || (res != FR_OK))
            {
                Error_Handler();
                serial_printf("bytes error %d\r\n", res);
            }
            else
            {

                f_close(&SDFile);
            }
        }
    }

    serial_printf("file written\r\n");
}
