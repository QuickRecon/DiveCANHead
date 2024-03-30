#include "DiveO2Cell.h"

const unsigned long baudRate = 9600;

const unsigned long temperature = 2250; //22.5C


DiveO2Cell::DiveO2Cell(HardwareSerial* inSerialPort){
  serialPort = inSerialPort;
  serialPort->begin(baudRate);
}

DiveO2Cell::~DiveO2Cell(){
  serialPort->end();
}

void DiveO2Cell::SetPPO2(float inPPO2){
  PPO2 = inPPO2;
}

void DiveO2Cell::Poll(){
  if(Serial.available()){
    unsigned long intPPO2 = PPO2*10000;
    serialPort->print("#DOXY ");
    serialPort->print(intPPO2);
    serialPort->print(" ");
    serialPort->print(temperature);
    serialPort->print(" ");
    serialPort->print(0);
    serialPort->write(0x0D);
  }
}
