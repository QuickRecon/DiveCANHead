#ifndef O2SCell_h
#define O2SCell_h

#include "Arduino.h"
#include "IDigitalCell.h"

class O2SCell : public IDigitalCell
{
public:
  explicit O2SCell(HardwareSerial *inSerialPort) : serialPort(inSerialPort)
  {
    serialPort->begin(baudRate);
  };

  ~O2SCell() {
    serialPort->end();
  };

  void SetPPO2(float inPPO2) override;
  void Poll() override;

private:
  const unsigned long baudRate = 115200;
  HardwareSerial *serialPort;
  float PPO2 = 0.2;
};

#endif
