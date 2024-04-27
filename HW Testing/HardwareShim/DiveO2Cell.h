#ifndef DiveO2Cell_h
#define DiveO2Cell_h

#include "Arduino.h"
#include "IDigitalCell.h"

class DiveO2Cell : public IDigitalCell
{
public:
  explicit DiveO2Cell(HardwareSerial *inSerialPort) : PPO2(0.2), serialPort(inSerialPort)
  {
    serialPort->begin(baudRate);
  };

  ~DiveO2Cell() = default;

  void SetPPO2(float inPPO2) override;
  void Poll() override;

private:
  const unsigned long baudRate = 19200;
  HardwareSerial *serialPort;
  float PPO2;
};

#endif