// The goal of this code is to effectively move information from the python test code (which interacts with us over Serial0)
// and move it into the "real" world where the DiveCAN board can pick it up, this code also needs to communicate the state
// of the board back to the test code so that we can confirm that the board is doing the right things.
// The peripherals that we emulate are:
// - Digital oxygen cells (currently only DiveO2, eventually oxygen scientific and Pyroscience)
// - Analog oxygen cells
// - DiveCAN Enable pin

// We also monitor a few states/more general parameters
// - Digital IO for GPIOs 1 & 2
// - Analog input for the DiveCAN board's cell outputs
// - VBus voltage

// Our test board (an arduino Due) runs far faster than our DiveCAN board so at least at this stage
// the code is implemented in a polling architecture, waiting for messages, then bundling up a response
// or taking the appropriate action.

// TODO:
// - MVP implementation
// - Digital Cells last polled
// - Digital Cell Error states
// - Control of USB PSUs
// - Solenoid testing

#include "DiveO2Cell.h"
#include "AnalogCell.h"
#include <Adafruit_ADS1X15.h>


// Arcane SAM BS yanked shamelessly from https://forum.arduino.cc/t/due-software-reset/332764/6
// probably works, I don't want to learn the datasheet of yet another uController for this project
#define SYSRESETREQ    (1<<2)
#define VECTKEY        (0x05fa0000UL)
#define VECTKEY_MASK   (0x0000ffffUL)
#define AIRCR          (*(uint32_t*)0xe000ed0cUL) // fixed arch-defined address
#define REQUEST_EXTERNAL_RESET (AIRCR=(AIRCR&VECTKEY_MASK)|VECTKEY|SYSRESETREQ)

Adafruit_ADS1115 ads;

IDigitalCell* dCell1;
IDigitalCell* dCell2;
IDigitalCell* dCell3;

AnalogCell* aCell1;
AnalogCell* aCell2;
AnalogCell* aCell3;

const int GPIO1Pin = 47;
const int GPIO2Pin = 50;
const int enPin = 53;
const int VBusPin = 8;

void(* resetFunc) (void) = 0; //declare reset function @ address 0 to jump back to the start of the program

void setup() {
  Serial.begin(9600);
  Serial.println("HW Shim Active");


  // Init SS Cell
  dCell1 =  new DiveO2Cell(&Serial1);
  dCell2 =  new DiveO2Cell(&Serial2);
  dCell3 =  new DiveO2Cell(&Serial3);

  // Init analog cells
  aCell1 = new AnalogCell(1);
  aCell2 = new AnalogCell(2);
  aCell3 = new AnalogCell(3);

  // Init ADC
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS.");
    while (1);
  }
  ads.setGain(GAIN_EIGHT); // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV

}

// Serial Messages
// sdc,n,x: Set Digital Cell for cell number n to value x
// sac,n,x: Set Analog Cell for cell number n value x
// sdcen: Send EN pin low (active)
// sdcden: Send EN pin high (off)
// gvbus: Get VBUS voltage
// gc,n: Get the millvolts for cell output n
// sio,n,i: Set io n to either 1 or 0
// gio,n: Get state of GPIO, puts pin in highZ state
// rst: Reset the shim

void loop() {
  
  // First run through and poll the digital cells to see if they need to respond to traffic
  dCell1->Poll();
  dCell2->Poll();
  dCell3->Poll();

  // Check if we have things to respond to
  if(Serial.available()){
    String inboundMsg = Serial.readString();
    Serial.println("Parsing str");
    // Detokenize the inbound string
    char *strings[10];
    char *ptr = NULL;
    byte index = 0;

    char inMsg[255] = {0};
    strncpy(inMsg, inboundMsg.c_str(),255);
    ptr = strtok(inMsg, ",");  // takes a list of delimiters
    while(ptr != NULL && index < 10)
    {
        strings[index] = ptr;
        index++;
        ptr = strtok(NULL, ",");  // takes a list of delimiters
    }

    // If tower of destiny as we check all the messages
    if(strcmp(strings[0], "sdc") == 0){
      int cellNum = String(strings[1]).toInt();
      float PPO2 = String(strings[2]).toFloat();
      switch(cellNum){
        case 1:
          dCell1->SetPPO2(PPO2);
          break;
        case 2:
          dCell2->SetPPO2(PPO2);
          break;
        case 3:
          dCell3->SetPPO2(PPO2);
          break;
      }
    } else if(strcmp(strings[0], "sac") == 0){
      int cellNum = String(strings[1]).toInt();
      float millis = String(strings[2]).toFloat();
      switch(cellNum){
        case 1:
          aCell1->SetMillis(millis);
          break;
        case 2:
          aCell2->SetMillis(millis);
          break;
        case 3:
          aCell3->SetMillis(millis);
          break;
      }
    } else if(strcmp(strings[0], "sdcen") == 0){
      pinMode(enPin, OUTPUT);
      digitalWrite(enPin, LOW);
    } else if(strcmp(strings[0], "sdcden") == 0){
      pinMode(enPin, OUTPUT);
      digitalWrite(enPin, HIGH);
    } else if(strcmp(strings[0], "gvbus") == 0){
      
    } else if(strcmp(strings[0], "gc") == 0){
      
    } else if(strcmp(strings[0], "sio") == 0){
      
    } else if(strcmp(strings[0], "gio") == 0){
      
    } else if(strcmp(strings[0], "rst") == 0) {
      REQUEST_EXTERNAL_RESET;
    } else {
      // A message we don't know
      Serial.println("ERR: Unknown command sent to shim");
    }

  }

}
