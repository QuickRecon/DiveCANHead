#ifndef DiveO2Cell_h
#define DiveO2Cell_h

#include "Arduino.h"
#include "IDigitalCell.h"

class DiveO2Cell : public IDigitalCell {
  public:
    DiveO2Cell(HardwareSerial* inSerialPort);
    ~DiveO2Cell();

    void SetPPO2(float inPPO2) override;
    void Poll() override;
  private:
    HardwareSerial* serialPort;
    float PPO2;
};

#endif