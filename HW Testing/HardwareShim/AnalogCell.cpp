#include "AnalogCell.h"
#include <math.h>


const float R1 = 2000.0f;
const float R2 = 100.0f;

const float frequency = 10000.0f;

const float calMap[4] =  {1.0f, (100.0f/95.4f), (100.0f/97.2f)*(48.0f/47.5f), (100.0f/97.2f)*(96.0f/94.7f)};

const unsigned int pinMap[4] = {0, 2, 3, 4};

//inline double min(double a, double b) { return ((a) < (b) ? (a) : (b)); }
//inline double max(double a, double b) { return ((a) > (b) ? (a) : (b)); }
AnalogCell::AnalogCell(int inCellNum) : cellNum(inCellNum), millis(0)
{
  PWM_Instance = new SAMDUE_PWM(pinMap[cellNum], frequency, 0);
}

void AnalogCell::SetMillis(float inMillis)
{
  float vMax = (3.3f * R2) / (R1 + R2) * 1000;
  float duty = (inMillis / vMax) * calMap[cellNum];
  //analogWrite(pinMap[cellNum], duty);
  PWM_Instance->setPWM(pinMap[cellNum], frequency, duty*100.0f);

  // Calculate how long it will take to get within a few millis of proper
  // We delay for that long to ensure that the user isn't searching for a value we haven't reached yet
  float deltaMillis = abs(inMillis - millis);
  millis = inMillis;
  float reqPercentage = Min(1 / deltaMillis, 1); // Decimal percentage we need to reach to be within a millivolt

  float nTC = log(reqPercentage) / log(1 - 0.632);

  float secondsDelay = nTC * 0.094; // Time constant for the shim
  int millisDelay = max(secondsDelay * 1000 - 400, 0);
  Serial.println("Delay " + String(millisDelay) + "dm:" + String(deltaMillis));
  delay(millisDelay);
}