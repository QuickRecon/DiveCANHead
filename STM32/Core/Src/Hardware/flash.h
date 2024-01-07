#ifndef _FLASH_H
#define _FLASH_H

#include "../Sensors/OxygenCell.h"

#ifdef __cplusplus
extern "C"
{
#endif
    bool GetCalibration(uint8_t cellNumber, CalCoeff_t *calCoeff);
    bool SetCalibration(uint8_t cellNumber, CalCoeff_t calCoeff);

#ifdef __cplusplus
}
#endif
#endif
