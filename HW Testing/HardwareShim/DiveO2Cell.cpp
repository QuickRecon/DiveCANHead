#include "DiveO2Cell.h"

const unsigned long temperature = 2250; //22.5C


DiveO2Cell::DiveO2Cell(HardwareSerial* inSerialPort) : PPO2(0.2), serialPort(inSerialPort)
{
  serialPort->begin(baudRate);
};

void DiveO2Cell::SetPPO2(float inPPO2){
  PPO2 = inPPO2;
}

void DiveO2Cell::Poll(){
  if(serialPort->available()){
    while(serialPort->read() != -1){
      // Clear the register contents
    } 
    unsigned long intPPO2 = PPO2*10000;
    String respStr = "#DRAW ";
    respStr += String(intPPO2);
    respStr += String(" ");
    respStr += String(temperature);
    respStr += String(" 0 0 0 0 999734 40365");
    respStr += String((char)0x0D);
    serialPort->print(respStr);
    serialPort->flush();
  }
}
