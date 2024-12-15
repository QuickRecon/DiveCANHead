#ifndef IDigitalCell_h
#define IDigitalCell_h

#include "Arduino.h"

class IDigitalCell
{
public:
  IDigitalCell() = default;
  ~IDigitalCell() = default;

  virtual void SetPPO2(float inPPO2);
  virtual void Poll();
};

#endif