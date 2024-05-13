#pragma once

#include "../common.h"
#include "Transciever.h"
#include "DiveCAN.h"

void ProcessMenu(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, Configuration_t * const configuration);
