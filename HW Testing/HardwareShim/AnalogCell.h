#ifndef AnalogCell_h
#define AnalogCell_h

#include "Arduino.h"

class AnalogCell {
  public:
    explicit AnalogCell(int inCellNum);
    ~AnalogCell() = default;

    void SetMillis(float inMillis);
  private:
    int cellNum;
};

#endif