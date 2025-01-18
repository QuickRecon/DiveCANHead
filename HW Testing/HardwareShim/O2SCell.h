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

    // Send the preamble string to make sure it gets skipped appropriately
    String respStr = "v;0.276;9.346;500.561;25.437;2.448;5788512768.00;-4248873216.00;12500;0.198;202300071";
    respStr += String((char)0x0D);
    serialPort->print(respStr);
    serialPort->flush();
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
  int timeOfLastSend = 0;
};

#endif
