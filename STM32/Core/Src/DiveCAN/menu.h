#ifndef __MENU_H
#define __MENU_H

#include "../common.h"
#include "Transciever.h"
#include "DiveCAN.h"

void ProcessMenu(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec);

#endif
