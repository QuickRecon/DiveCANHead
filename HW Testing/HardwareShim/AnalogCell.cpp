#include "AnalogCell.h"

const float R1 = 2000;
const float R2 = 100;

const unsigned int pinMap[4] = {0,2,3,4};

AnalogCell::AnalogCell(int inCellNum){
  cellNum = inCellNum;
}

void AnalogCell::SetMillis(float inMillis){
  float vMax = (3.3f*R2)/(R1+R2) *1000;
  int duty = (inMillis/vMax)*255.0f;
  analogWrite(pinMap[cellNum], duty);
}