#include "errors.h"

void NonFatalError(NonFatalError_t error)
{
    // TODO: this should lodge the error in EEPROM emulation so we can recover it/lodge it later
}

void NonFatalErrorISR(NonFatalError_t error)
{
    // Should do same kind of thing as NonFatalError, but quickly and isr safe
}

void FatalError(FatalError_t error)
{
    // TODO: this should lodge the error in EEPROM, then reset (or maybe we just wait for the iwdg to reset us?)
}