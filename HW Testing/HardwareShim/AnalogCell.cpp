#include "AnalogCell.h"
#include <array>
#include <math.h>

const float R1 = 2000;
const float R2 = 100;

const std::array<const unsigned int,4> pinMap = {0,2,3,4};
inline double min(double a, double b) { return ((a)<(b) ? (a) : (b)); }
inline double max(double a, double b) { return ((a)>(b) ? (a) : (b)); }
AnalogCell::AnalogCell(int inCellNum): cellNum(inCellNum), millis(0){

}

void AnalogCell::SetMillis(float inMillis){
  float vMax = (3.3f*R2)/(R1+R2) *1000;
  int duty = (inMillis/vMax)*255.0f*1.04f;
  analogWrite(pinMap[cellNum], duty);

  // Calculate how long it will take to get within a few millis of proper
  // We delay for that long to ensure that the user isn't searching for a value we haven't reached yet
  float deltaMillis = abs(inMillis - millis);
  millis = inMillis;
  float reqPercentage = min(1/deltaMillis,1); // Decimal percentage we need to reach to be within a millivolt

  float nTC = log(reqPercentage)/log(1-0.632);
  
  float secondsDelay = nTC*0.094; // Time constant for the shim
  int millisDelay = max(secondsDelay*1000 - 500,0);
  Serial.println("Delay " + String(millisDelay) + "dm:"+String(deltaMillis));
  delay(millisDelay);
}