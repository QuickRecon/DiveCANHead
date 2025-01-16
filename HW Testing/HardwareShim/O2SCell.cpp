#include "O2SCell.h"

const unsigned long temperature = 2250; // 22.5C

void O2SCell::SetPPO2(float inPPO2)
{
  PPO2 = inPPO2/100;
}

void O2SCell::Poll()
{
  if (serialPort->available())
  {
    while (serialPort->read() != -1)
    {
      // Clear the register contents
    }
    char buff[10] = {0};
    snprintf(buff, sizeof(buff), "%5.3f", PPO2);
    String respStr = "Mm: ";
    respStr += String(buff);
    respStr += String((char)0x0D);
    serialPort->print(respStr);
    serialPort->flush();
  }
}
