#ifndef AnalogCell_h
#define AnalogCell_h

#include "Arduino.h"

#include "SAMDUE_PWM.h"

class AnalogCell
{
public:
  explicit AnalogCell(int inCellNum);
  ~AnalogCell() = default;

  void SetMillis(float inMillis);

private:
  float millis;
  SAMDUE_PWM* PWM_Instance;
  int cellNum;
};

#endif