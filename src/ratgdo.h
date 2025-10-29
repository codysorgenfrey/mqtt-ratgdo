/************************************
 * Rage
 * Against
 * The
 * Garage
 * Door
 * Opener
 *
 * Copyright (C) 2022  Paul Wieland
 *
 * GNU GENERAL PUBLIC LICENSE
 ************************************/

#ifndef _RATGDO_H
#define _RATGDO_H

#include "common.h"
#include "SoftwareSerial.h" // Using espsoftwareserial https://github.com/plerup/espsoftwareserial

extern SoftwareSerial swSerial;

/********************************** CONFIG DEFINITIONS *****************************************/
extern String controlProtocol; // default control protocol (secplus2 | secplus1 | drycontact)
#ifndef VERSION
#define VERSION 2.29
#endif

/********************************** PIN DEFINITIONS *****************************************/
#define INPUT_GDO D2 // 
#define OUTPUT_GDO D4 // D1 // D4 // red control terminal / GarageDoorOpener (UART1 TX) pin is D4 on D1 Mini
#define TRIGGER_OPEN D5 // dry contact for opening door
#define TRIGGER_CLOSE D6 // dry contact for closing door
#define TRIGGER_LIGHT D3 // dry contact for triggering light (no discrete light commands, so toggle only)
#define STATUS_DOOR D0 // output door status, HIGH for open, LOW for closed
#define STATUS_OBST D8 // output for obstruction status, HIGH for obstructed, LOW for clear
#define INPUT_OBST D7 // black obstruction sensor terminal


/********************************** STATE *****************************************/
extern uint8_t doorState;
extern uint8_t lightState;
extern uint8_t lockState;
extern uint8_t motionState;
extern uint8_t obstructionState;

/********************************** GLOBAL VARS *****************************************/
extern bool setupComplete;
extern bool ignoredRetained;
extern unsigned int setupCompleteMillis;
extern unsigned int rollingCodeCounter;
extern unsigned int idCode;
extern byte txSP1StaticCode[4];
extern byte rxSP1StaticCode[SECPLUS1_CODE_LEN];
extern byte secplus1States[19];

extern byte txSP2RollingCode[SECPLUS2_CODE_LEN];
extern byte rxSP2RollingCode[SECPLUS2_CODE_LEN];

extern unsigned int obstructionLowCount;  // count obstruction low pulses
extern bool obstructionSensorDetected;
extern unsigned long lastObstructionHigh;  // count time between high pulses from the obst ISR
extern unsigned long lastRX;

extern bool dryContactDoorOpen;
extern bool dryContactDoorClose;
extern bool dryContactToggleLight;

/********************************** FUNCTION DECLARATION *****************************************/
void setupRATGDO();
void loopRATGDO();

void blink(bool trigger);
void sync();

void toggleDoor();
void openDoor();
void closeDoor();
void stopDoor();

void toggleLight();
void lightOn();
void lightOff();

void toggleLock();
void lock();
void unlock();

#endif