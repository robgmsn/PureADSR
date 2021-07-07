/*

  GMSN Pure ADSR v20170915
  Rob Spencer 2017
  cc-by 4.0

  Full Open Source Documentation at https://gmsn.co.uk/pure-adsr including hardware files, Bill of Materials, Mouser Cart and Help Videos

  For more indepth build and general support chat, join us at the GMSN! Synth Design Slack: https://join.slack.com/t/gmsnsynthdesign/shared_invite/MjE0NzM1ODc3NDkyLTE1MDA0NTI1MTItODQ3MDM4OTdlYw

*/

#include "SPI.h"

//Setup pin variables
const byte dacCS = 10; //14
const byte trigIn = 2; //32
const byte trigBut = 4; //2
const byte modeSw1 = 6; //10
const byte modeSw2 = 7; //11

const byte knob1 = A3; //26
const byte knob2 = A2; //25
const byte knob3 = A1; //24
const byte knob4 = A0; //23

//Setup envelope variable
int aPot, enVal = 0, dPot, sPot, sVal, rPot, onPot, offPot;

//Setup state variables
boolean gate = 0, rising = 0;
byte mode, phase = 0;
int trapOnCount = 0, trapOffCount = 0;

//Setup switch debounce variables
int buttonState, lastButtonState = HIGH;
unsigned long lastDebounceTime = 0, debounceDelay = 50;


void setup() {

  //Start DAC Comms
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);

  //Configure Pins
  pinMode(dacCS, OUTPUT); //DAC CS
  pinMode(trigIn, INPUT); //TRIGGER IN
  pinMode(trigBut, INPUT); //Button
  pinMode(modeSw1, INPUT); //MODE SW1
  pinMode(modeSw2, INPUT); //MODE SW2
  digitalWrite(dacCS, HIGH);

  attachInterrupt(digitalPinToInterrupt(trigIn), gateOn, FALLING);



}

void loop() {

  //Poll the Mode switch to set the mode
  if (digitalRead(modeSw2) == HIGH) {
    mode = 1;
  } else if (digitalRead(modeSw1) == LOW && digitalRead(modeSw2) == LOW) {
    mode = 2;
  } else {
    mode = 3;
  }

 

  //Control logic which calls the functions
  switch (phase) {

    //Phase 0 is idle, but if it's in the Trap mode it kicks it back into the Attack phase.
    case 0:
      if (mode == 3) {
        phase = 1;
      }
      break;

    //Attack Phase with some logic to kick it into the correct next phase depending on the Mode.
    case 1:
      attack();

      if (enVal >= 4095) {
        switch (mode) {
          case 1:
            phase = 2;
            break;

          case 2:
            phase = 3;
            break;

          case 3:
            phase = 4;
            break;
        }
      }

      if (mode == 1 && digitalRead(trigBut) == HIGH && digitalRead(trigIn) == HIGH) {
        phase = 3;
      }
      break;

    //Decay to Sustain Phase.
    case 2:
      decaySustain();
      if (mode == 1 && digitalRead(trigBut) == HIGH && digitalRead(trigIn) == HIGH) {
        phase = 3;
      }
      break;

    //Release Phase. If it's in ADSR or AR mode, then knob 4 is the Release. If it's in Trap mode, then it's knob 3.
    case 3:
      if (mode == 3) {
        releasePhase(knob3);
      } else {
        releasePhase(knob4);
      }
      if (enVal < 100) {
        enVal = 0;
        if (mode == 3) {
          phase = 5;
        } else {
          phase = 0;
        }
      }
      break;

    //Phase 4, Trap On
    case 4:
      trapOn();
      break;

    //Phase 5, Trap Off
    case 5:
      trapOff();
      break;
  }


  //Poll Trigger Button, debounce, initialise Up phase
  trigButton();
}

void attack() {
  aPot = fscale(0, 1024, 100, 1, analogRead(knob1), 10);
  if (aPot > 2999) {
    enVal = 4095;
  } else {
    enVal = enVal + aPot;
    if (enVal > 4090) {
      enVal = 4095;
    }
  }
  mcpWrite((int)enVal);
}

void decaySustain() {
  dPot = fscale(0, 1024, 100, 1, analogRead(knob2), 10);
  sPot = map(analogRead(knob3), 0, 1024, 0, 4096);
  if (enVal <= sPot) {
    enVal = sPot;
  } else {
    enVal = enVal - dPot;
  }
  mcpWrite((int)enVal);
}

void releasePhase(byte knob) {
  rPot = fscale(0, 1024, 100, 0, analogRead(knob), 10);
  enVal = enVal - rPot;
  mcpWrite((int)enVal);
}

void trapOn() {
  mcpWrite((int)enVal);
  onPot = fscale(0, 1024, 0, 30000, analogRead(knob2), -7);
  if (trapOnCount >= onPot) {
    trapOnCount = 0;
    phase = 3;
  } else {
    trapOnCount++;
  }
}

void trapOff() {
  mcpWrite((int)enVal);
  offPot = fscale(0, 1024, 0, 30000, analogRead(knob4), -7);
  if (trapOffCount >= offPot) {
    trapOffCount = 0;
    phase = 1;
  } else {
    trapOffCount++;
  }
}


/*Debounce routine for the switch. Switches are very noises when they are switching.
  When you press a switch, the mechanism can "bounce" on, off, on, off, on, etc, in the space of a few milliseconds.
  This debounce function removes the noise.*/
void trigButton() {
  int reading = digitalRead(trigBut);

  if ((millis() - lastDebounceTime) > debounceDelay) {

    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        gateOn();
        lastDebounceTime = millis();
      }
    }
  }
  lastButtonState = reading;
}

//Interrupt routine for rising edge of Gate
void gateOn() {
  if (phase == 1) {
    enVal = 1;
  } else {
    phase = 1;
  }
  trapOffCount = 0;
  trapOnCount = 0;
}

/*Writing to the DAC.
  The whole purpose of the module is to output a voltage envelope. That voltage then controls another module, such as a VCA or a filter.
  In order to get that voltage out, we need to convert the value of the "envValue" variable into a voltage, which is done by a Digital to Analogue Converter, or DAC for short.
  The function below send envValue to the DAC for conversion to the analogue voltage.
  0 = Off 4095 = Full on.*/
void mcpWrite(int value) {
  value = value + 28672;
  digitalWrite(dacCS, LOW);
  SPI.transfer16(value);
  digitalWrite(dacCS, HIGH);
}
