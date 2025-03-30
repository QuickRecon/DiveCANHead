#include "O2SCell.h"

const unsigned long temperature = 2250;  // 22.5C

void O2SCell::SetPPO2(float inPPO2) {
  PPO2 = inPPO2 / 100;
}

void O2SCell::Poll() {
  if (serialPort->available()) {
    String msg = serialPort->readStringUntil(0x0A);
    if (msg == "Mm") {
      char buff[10] = { 0 };
      snprintf(buff, sizeof(buff), "%5.3f", PPO2);
      String respStr = "Mn:";
      respStr += String(buff);
      respStr += String((char)0x0D);
      txMsg(respStr);
      timeOfLastSend = millis();
    } else {

      Serial.print("Unknown cmd: ");

      Serial.println(msg);
    }
  } else if (millis() - timeOfLastSend > 500) {
    char buff[10] = { 0 };
    snprintf(buff, sizeof(buff), "%5.3f", PPO2);
    String respStr = "Mn:";
    respStr += String(buff);
    respStr += String((char)0x0D);
    txMsg(respStr);
    timeOfLastSend = millis();
  }
}
