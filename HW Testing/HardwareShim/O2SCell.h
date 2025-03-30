#ifndef O2SCell_h
#define O2SCell_h

#include "Arduino.h"
#include "IDigitalCell.h"
#include <ArduinoSTL.h>
#include <map>

class O2SCell : public IDigitalCell {
public:

  void enTX(HardwareSerial *inSerialPort) {
    usart_config[inSerialPort]->US_CR &= ~(1 << 7);  //Set TXDIS Low
    usart_config[inSerialPort]->US_MR &= ~(11 << 14);  //Set Mode 0
  }

  void disTX(HardwareSerial *inSerialPort) {
    usart_config[inSerialPort]->US_CR |= 1 << 7;  //Set TXDIS High
    usart_config[inSerialPort]->US_MR |= (1 << 14);  //Set Mode 1
  }

  explicit O2SCell(HardwareSerial *inSerialPort)
    : serialPort(inSerialPort) {
    serialPort->begin(baudRate);

    digitalWrite(bridge_pin[serialPort], HIGH);
    // Send the preamble string to make sure it gets skipped appropriately
    String respStr = "v;0.276;9.346;500.561;25.437;2.448;5788512768.00;-4248873216.00;12500;0.198;202300071";
    respStr += String((char)0x0D);
    enTX(serialPort);
    serialPort->print(respStr);
    serialPort->flush();

    delay(((respStr.length() * 8 * 1000) / baudRate) + 1);
    disTX(serialPort);
    while (serialPort->read() != -1) {
      // Clear the register contents
    }
  };

  ~O2SCell() {
    digitalWrite(bridge_pin[serialPort], LOW);
    serialPort->end();
  };

  void txMsg(String msg) {

    Serial.print("Sending: ");
    Serial.println(msg);
    enTX(serialPort);
    serialPort->print(msg);
    serialPort->flush();
    // Estimate how long by the length of the string and the baud rate
    delay(((msg.length() * 8 * 1000) / baudRate) + 1);
    disTX(serialPort);
  }

  void SetPPO2(float inPPO2) override;
  void Poll() override;

private:

  std::map<HardwareSerial *, Usart *> usart_config = { { &Serial1, USART1 }, { &Serial2, USART2 }, { &Serial3, USART3 } };

  // O2S cells require RX and TX on the same line so we use a GPIO line to bridge the GPIO while TXing
  std::map<HardwareSerial *, int> bridge_pin = { { &Serial1, 33 }, { &Serial2, 35 }, { &Serial3, 37 } };
  const unsigned long baudRate = 115200;
  HardwareSerial *serialPort;
  float PPO2 = 0.2;
  int timeOfLastSend = 0;
};

#endif
