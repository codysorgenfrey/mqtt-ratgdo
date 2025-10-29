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

#include <LittleFS.h>
#include "ratgdo.h"
#include "rolling_code.h"
#include "static_code.h"

 /********************************** VARIABLE DEFINITIONS *****************************************/
SoftwareSerial swSerial;

// CONFIG DEFINITIONS
String controlProtocol = "secplus2"; // default control protocol (secplus2 | secplus1 | drycontact)

// STATE
uint8_t doorState = 0;
String doorStates[7] = { "unknown","open","closed","stopped","opening","closing","syncing" };
NewStateCallback doorStateCallback = nullptr;

uint8_t lightState = 2;
String lightStates[3] = { "off","on","unknown" };
NewStateCallback lightStateCallback = nullptr;

uint8_t lockState = 2;
String lockStates[3] = { "unlocked","locked","unknown" };
NewStateCallback lockStateCallback = nullptr;

uint8_t motionState = 0;
String motionStates[2] = { "clear","detected" };
NewStateCallback motionStateCallback = nullptr;

uint8_t obstructionState = 2;
String obstructionStates[3] = { "obstructed","clear","unknown" };
NewStateCallback obstructionStateCallback = nullptr;

// GLOBAL VARS
bool setupComplete = false;
bool ignoredRetained = false;
unsigned int setupCompleteMillis;
unsigned int rollingCodeCounter;
unsigned int idCode;
byte txSP1StaticCode[4];
byte rxSP1StaticCode[SECPLUS1_CODE_LEN];
byte secplus1States[19] = { 0x35,0x35,0x35,0x35,0x33,0x33,0x53,0x53,0x38,0x3A,0x3A,0x3A,0x39,0x38,0x3A, 0x38,0x3A,0x39,0x3A };

byte txSP2RollingCode[SECPLUS2_CODE_LEN];
byte rxSP2RollingCode[SECPLUS2_CODE_LEN];

unsigned int obstructionLowCount = 0;  // count obstruction low pulses
bool obstructionSensorDetected = false;
unsigned long lastObstructionHigh = 0;  // count time between high pulses from the obst ISR
unsigned long lastRX = 0;

bool dryContactDoorOpen = false;
bool dryContactDoorClose = false;
bool dryContactToggleLight = false;

/************************* DOOR COMMUNICATION *************************/
/*
 * Transmit a message to the door opener over uart1
 * The TX1 pin is controlling a transistor, so the logic is inverted
 * A HIGH state on TX1 will pull the 12v line LOW
 *
 * The opener requires a specific duration low/high pulse before it will accept a message
 */
void transmit(byte* payload, unsigned int length) {
  if (controlProtocol == "secplus2") {
    digitalWrite(OUTPUT_GDO, HIGH); // pull the line high for 1305 micros so the door opener responds to the message
    delayMicroseconds(1305);
    digitalWrite(OUTPUT_GDO, LOW); // bring the line low

    delayMicroseconds(1260); // "LOW" pulse duration before the message start

    swSerial.write(payload, length);
  }
  else if (controlProtocol == "secplus1") {
    if (length == 1) {
      swSerial.write(payload, 1);
      return;
    }

    uint8_t txDelayLen;
    byte tempPayload[1];

    txDelayLen = (lastRX + 275) - millis();
    delay(txDelayLen);

    for (uint8_t i = 0; i <= length; i++) {
      tempPayload[0] = payload[i];
      swSerial.write(tempPayload, 1);
      delay(25);
    }
  }
}

void pullLow() {
  digitalWrite(OUTPUT_GDO, HIGH);
  delay(500);
  digitalWrite(OUTPUT_GDO, LOW);
}

void blink(bool trigger) {
  if (LED_BUILTIN == OUTPUT_GDO) return;
  static unsigned int onMillis = 0;
  unsigned int currentMillis = millis();

  if (trigger) {
    digitalWrite(LED_BUILTIN, LOW);
    onMillis = currentMillis;
  }
  else if (currentMillis - onMillis > 500) {
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

void sync() {
  if (controlProtocol != "secplus2") {
    Serial.println("sync only needed with security+ 2.0");
    return;
  }

  getRollingCode("reboot1");
  transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
  delay(65);

  getRollingCode("reboot2");
  transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
  delay(65);

  getRollingCode("reboot3");
  transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
  delay(65);

  getRollingCode("reboot4");
  transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
  delay(65);

  getRollingCode("reboot5");
  transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
  delay(65);

  getRollingCode("reboot6");
  transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
  delay(65);

  writeCounterToFlash("rolling", rollingCodeCounter);
}

/*************************** DETECTING THE DOOR STATE ***************************/
void wallPanelEmulatorLoop() {
  static bool wallPanelDetected = false;
  if (controlProtocol != "secplus1" || wallPanelDetected) return;

  unsigned long currentMillis = millis();
  static unsigned long lastRequestMillis = 0;
  static bool emulateWallPanel = false;
  static unsigned long serialDetected = 0;
  static uint8_t stateIndex = 0;

  if (!serialDetected) {
    if (swSerial.available()) {
      serialDetected = currentMillis;
    }

    return;
  }

  // 
  if (currentMillis - serialDetected < 35000 || doorState == 6) {
    if (currentMillis - lastRequestMillis > 2000) {
      Serial.println("Looking for security+ 1.0 wall panel...");
      lastRequestMillis = currentMillis;
    }

    if (!wallPanelDetected && (doorState != 0 || lightState != 2)) {
      wallPanelDetected = true;
      Serial.println("Wall panel detected.");
      return;
    }
  }
  else {
    if (!emulateWallPanel && !wallPanelDetected) {
      emulateWallPanel = true;
      Serial.println("No wall panel detected. Switching to emulation mode.");
    }

    if (emulateWallPanel && currentMillis - lastRequestMillis > 250) {
      lastRequestMillis = currentMillis;
      txSP1StaticCode[0] = byte(secplus1States[stateIndex]);
      transmit(txSP1StaticCode, 1);
      stateIndex++;
      if (stateIndex == 18) stateIndex = 15;
    }
  }
}

void gdoStateLoop() {
  blink(false);
  if (!swSerial.available()) return;
  uint8_t serData = swSerial.read();

  static uint32_t msgStart;
  static bool reading = false;
  static uint16_t byteCount = 0;

  if (!reading) {
    // shift serial byte onto msg start
    msgStart <<= 8;
    msgStart |= serData;

    if (controlProtocol == "secplus2") {
      // truncate to 3 bytes
      msgStart &= 0x00FFFFFF;

      // if we are at the start of a message, capture the next 16 bytes
      if (msgStart == 0x550100) {
        byteCount = 3;
        rxSP2RollingCode[0] = 0x55;
        rxSP2RollingCode[1] = 0x01;
        rxSP2RollingCode[2] = 0x00;

        reading = true;
        return;
      }
    }

    if (controlProtocol == "secplus1") {
      // truncate to 1 byte;
      msgStart &= 0x000000FF;

      if (msgStart >= 0x30 && msgStart <= 0x3A) {
        rxSP1StaticCode[0] = msgStart;
        byteCount = 1;
        reading = true;
        lastRX = millis();
        return;
      }
    }
  }

  if (reading) {
    if (controlProtocol == "secplus2") {
      rxSP2RollingCode[byteCount] = serData;
      byteCount++;

      if (byteCount == SECPLUS2_CODE_LEN) {
        reading = false;
        msgStart = 0;
        byteCount = 0;

        readRollingCode(rxSP2RollingCode, doorState, lightState, lockState, motionState, obstructionState);
      }
    }

    if (controlProtocol == "secplus1") {
      rxSP1StaticCode[byteCount] = serData;
      byteCount++;

      if (byteCount == 2) {
        reading = false;
        msgStart = 0;
        byteCount = 0;

        readStaticCode(rxSP1StaticCode, doorState, lightState, lockState);
      }
    }
  }
}

/*************************** DRY CONTACT CONTROL OF LIGHT & DOOR ***************************/
void IRAM_ATTR isrDebounce(const char* type) {
  static unsigned long lastOpenDoorTime = 0;
  static unsigned long lastCloseDoorTime = 0;
  static unsigned long lastToggleLightTime = 0;
  static bool lastDryContactDoorOpen = false;
  static bool lastDryContactDoorClose = false;
  unsigned long currentMillis = millis();

  // Prevent ISR during the first 2 seconds after reboot
  if (currentMillis < 2000) return;

  if (strcmp(type, "openDoor") == 0) {
    if (controlProtocol == "drycontact") {
      if (currentMillis - lastOpenDoorTime > 300) {
        dryContactDoorOpen = !digitalRead(TRIGGER_OPEN);

        if (dryContactDoorOpen != lastDryContactDoorOpen) {
          lastOpenDoorTime = currentMillis;
          lastDryContactDoorOpen = dryContactDoorOpen;
        }
      }

    }
    else {
      if (digitalRead(TRIGGER_OPEN) == LOW) {
        // save the time of the falling edge
        lastOpenDoorTime = currentMillis;
      }
      else if (currentMillis - lastOpenDoorTime > 100 && currentMillis - lastOpenDoorTime < 10000) {
        // now see if the rising edge was between 100ms and 10 seconds after the falling edge
        dryContactDoorOpen = true;
        blink(true);
      }
    }
  }

  if (strcmp(type, "closeDoor") == 0) {
    if (controlProtocol == "drycontact") {
      if (currentMillis - lastCloseDoorTime > 300) {
        dryContactDoorClose = !digitalRead(TRIGGER_CLOSE);

        if (dryContactDoorClose != lastDryContactDoorClose) {
          lastCloseDoorTime = currentMillis;
          lastDryContactDoorClose = dryContactDoorClose;
        }
      }

    }
    else {
      if (digitalRead(TRIGGER_CLOSE) == LOW) {
        // save the time of the falling edge
        lastCloseDoorTime = currentMillis;
      }
      else if (currentMillis - lastCloseDoorTime > 100 && currentMillis - lastCloseDoorTime < 10000) {
        // now see if the rising edge was between 100ms and 10 seconds after the falling edge
        dryContactDoorClose = true;
        blink(true);
      }
    }
  }

  if (strcmp(type, "toggleLight") == 0) {
    if (digitalRead(TRIGGER_LIGHT) == LOW) {
      // save the time of the falling edge
      lastToggleLightTime = currentMillis;
    }
    else if (currentMillis - lastToggleLightTime > 100 && currentMillis - lastToggleLightTime < 10000) {
      // now see if the rising edge was between 500ms and 10 seconds after the falling edge
      dryContactToggleLight = true;
    }
  }
}

void IRAM_ATTR isrDoorOpen() {
  isrDebounce("openDoor");
}

void IRAM_ATTR isrDoorClose() {
  isrDebounce("closeDoor");
}

void IRAM_ATTR isrLight() {
  isrDebounce("toggleLight");
}

// handle changes to the dry contact state
void dryContactLoop() {
  static bool previousDryContactDoorOpen = false;
  static bool previousDryContactDoorClose = false;

  if (dryContactDoorOpen) {
    if (controlProtocol == "drycontact") {
      doorState = 1;
    }
    else {
      Serial.println("Dry Contact: open the door");
      openDoor();
      dryContactDoorOpen = false;
    }
  }

  if (dryContactDoorClose) {
    if (controlProtocol == "drycontact") {
      doorState = 2;
    }
    else {
      Serial.println("Dry Contact: close the door");
      closeDoor();
      dryContactDoorClose = false;
    }
  }

  if (dryContactToggleLight) {
    Serial.println("Dry Contact: toggle the light");
    dryContactToggleLight = false;
    toggleLight();
  }

  if (controlProtocol == "drycontact") {
    if (!dryContactDoorClose && !dryContactDoorOpen) {
      if (previousDryContactDoorClose) {
        doorState = 4;
      }

      if (previousDryContactDoorOpen) {
        doorState = 5;
      }
    }

    if (previousDryContactDoorOpen != dryContactDoorOpen) {
      previousDryContactDoorOpen = dryContactDoorOpen;
    }
    if (previousDryContactDoorClose != dryContactDoorClose) {
      previousDryContactDoorClose = dryContactDoorClose;
    }
  }
}

/*************************** OBSTRUCTION DETECTION ***************************/
void IRAM_ATTR isrObstruction() {
  if (digitalRead(INPUT_OBST)) {
    lastObstructionHigh = millis();
  }
  else {
    obstructionSensorDetected = true;
    obstructionLowCount++;
  }

}

void obstructionLoop() {
  if (!obstructionSensorDetected) return;
  long currentMillis = millis();
  static unsigned long lastMillis = 0;

  // the obstruction sensor has 3 states: clear (HIGH with LOW pulse every 7ms), obstructed (HIGH), asleep (LOW)
  // the transitions between awake and asleep are tricky because the voltage drops slowly when falling asleep
  // and is high without pulses when waking up

  // If at least 3 low pulses are counted within 50ms, the door is awake, not obstructed and we don't have to check anything else

  // Every 50ms
  if (currentMillis - lastMillis > 50) {
    // check to see if we got between 3 and 8 low pulses on the line
    if (obstructionLowCount >= 3 && obstructionLowCount <= 8) {
      obstructionState = 1;

      // if there have been no pulses the line is steady high or low			
    }
    else if (obstructionLowCount == 0) {
      // if the line is high and the last high pulse was more than 70ms ago, then there is an obstruction present
      if (digitalRead(INPUT_OBST) && currentMillis - lastObstructionHigh > 70) {
        obstructionState = 0;
      }
      else {
        // asleep
      }
    }

    lastMillis = currentMillis;
    obstructionLowCount = 0;
  }
}

/*************************** STATUS UPDATES ***************************/

void setNewDoorStateCallback(NewStateCallback cb) {
  doorStateCallback = cb;
}

void sendDoorStatus() {
  Serial.print("Door state ");
  Serial.println(doorStates[doorState]);

  if (doorState == 1) digitalWrite(STATUS_DOOR, HIGH); // Open
  if (doorState == 2) digitalWrite(STATUS_DOOR, LOW); // Closed

  if (doorStateCallback != nullptr) {
    doorStateCallback(doorState);
  }
}

void setNewLightStateCallback(NewStateCallback cb) {
  lightStateCallback = cb;
}

void sendLightStatus() {
  Serial.print("Light state ");
  Serial.println(lightStates[lightState]);

  if (lightStateCallback != nullptr) {
    lightStateCallback(lightState);
  }
}

void setNewLockStateCallback(NewStateCallback cb) {
  lockStateCallback = cb;
}

void sendLockStatus() {
  Serial.print("Lock state ");
  Serial.println(lockStates[lockState]);

  if (lockStateCallback != nullptr) {
    lockStateCallback(lockState);
  }
}

void setNewMotionStateCallback(NewStateCallback cb) {
  motionStateCallback = cb;
}

void sendMotionStatus() {
  Serial.print("Motion state ");
  Serial.println(motionStates[motionState]);

  if (motionStateCallback != nullptr) {
    motionStateCallback(motionState);
  }

  motionState = 0; // reset motion state

  // query to sync light state
  delay(100);
  getRollingCode("reboot2");
  transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
}

void setObstructionStateCallback(NewStateCallback cb) {
  obstructionStateCallback = cb;
}

void sendObstructionStatus() {
  Serial.print("Obstruction status ");
  Serial.println(obstructionStates[obstructionState]);

  if (obstructionState == 0) digitalWrite(STATUS_OBST, HIGH); // obstructed
  if (obstructionState == 1) digitalWrite(STATUS_OBST, LOW); // clear

  if (obstructionStateCallback != nullptr) {
    obstructionStateCallback(obstructionState);
  }
}

void statusUpdateLoop() {
  // initialize to unknown
  static uint8_t previousDoorState = 0;
  static uint8_t previousLightState = 2;
  static uint8_t previousLockState = 2;
  static uint8_t previousObstructionState = 2;

  if (doorState != previousDoorState) sendDoorStatus();
  if (lightState != previousLightState) sendLightStatus();
  if (lockState != previousLockState) sendLockStatus();
  if (obstructionState != previousObstructionState) sendObstructionStatus();

  if (motionState == 1) {
    sendMotionStatus();
    motionState = 0;
  }

  previousDoorState = doorState;
  previousLightState = lightState;
  previousLockState = lockState;
  previousObstructionState = obstructionState;
}

// Door functions
void openDoor() {
  if (doorStates[doorState] == "open" || doorStates[doorState] == "opening") {
    Serial.print("The door is already ");
    Serial.println(doorStates[doorState]);
    return;
  }

  toggleDoor();
}

void closeDoor() {
  if (doorStates[doorState] == "closed" || doorStates[doorState] == "closing") {
    Serial.print("The door is already ");
    Serial.println(doorStates[doorState]);
    return;
  }

  toggleDoor();
}

void stopDoor() {
  if (doorStates[doorState] == "opening" || doorStates[doorState] == "closing") {
    toggleDoor();
  }
  else {
    Serial.print("The door is not moving.");
  }
}

void toggleDoor() {
  if (controlProtocol == "drycontact") {
    pullLow();
  }
  else if (controlProtocol == "secplus1") {
    getStaticCode("door");
    transmit(txSP1StaticCode, 4);
  }
  else {
    getRollingCode("door1");
    transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);

    delay(40);

    getRollingCode("door2");
    transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);

    writeCounterToFlash("rolling", rollingCodeCounter);
  }
}

// Light functions
void lightOn() {
  if (lightStates[lightState] == "on") {
    Serial.println("already on");
  }
  else {
    toggleLight();
  }
}

void lightOff() {
  if (lightStates[lightState] == "off") {
    Serial.println("already off");
  }
  else {
    toggleLight();
  }
}

void toggleLight() {
  if (controlProtocol == "drycontact") {
    Serial.println("Light control not supported with dry contact control.");
  }
  else if (controlProtocol == "secplus1") {
    getStaticCode("light");
    transmit(txSP1StaticCode, 4);
  }
  else {
    getRollingCode("light");
    transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
    writeCounterToFlash("rolling", rollingCodeCounter);
  }
}

// Lock functions
void lock() {
  if (lockStates[lockState] == "locked") {
    Serial.println("already locked");
  }
  else {
    toggleLock();
  }
}

void unlock() {
  if (lockStates[lockState] == "unlocked") {
    Serial.println("already unlocked");
  }
  else {
    toggleLock();
  }
}

void toggleLock() {
  if (controlProtocol == "secplus1") {
    getStaticCode("lock");
    transmit(txSP1StaticCode, 4);
  }
  else {
    getRollingCode("lock");
    transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
    writeCounterToFlash("rolling", rollingCodeCounter);
  }
}

/*************************** SETUP FUNCTION ***************************/
void setupRATGDO() {
  if (OUTPUT_GDO != LED_BUILTIN) {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
  }
  pinMode(INPUT_GDO, INPUT_PULLUP);
  pinMode(OUTPUT_GDO, OUTPUT);

  Serial.begin(115200); // must remain at 115200 for improv
  Serial.println("");

  pinMode(TRIGGER_OPEN, INPUT_PULLUP);
  pinMode(TRIGGER_CLOSE, INPUT_PULLUP);
  pinMode(TRIGGER_LIGHT, INPUT_PULLUP);
  pinMode(STATUS_DOOR, OUTPUT);
  pinMode(STATUS_OBST, OUTPUT);
  pinMode(INPUT_OBST, INPUT);

  attachInterrupt(TRIGGER_OPEN, isrDoorOpen, CHANGE);
  attachInterrupt(TRIGGER_CLOSE, isrDoorClose, CHANGE);
  attachInterrupt(TRIGGER_LIGHT, isrLight, CHANGE);
  attachInterrupt(INPUT_OBST, isrObstruction, CHANGE);

  delay(60); // 
  if (controlProtocol == "drycontact") {
    Serial.println("Using dry contact control");
  }
  else if (controlProtocol == "secplus1") {
    swSerial.begin(1200, SWSERIAL_8E1, INPUT_GDO, OUTPUT_GDO, true);
    Serial.println("Using security+ 1.0");
  }
  else {
    // default to secplus2
    controlProtocol = "secplus2";
    swSerial.begin(9600, SWSERIAL_8N1, INPUT_GDO, OUTPUT_GDO, true);
    Serial.println("Using security+ 2.0");
  }

  Serial.println("Setup Complete");
  Serial.println(" _____ _____ _____ _____ ____  _____ ");
  Serial.println("| __  |  _  |_   _|   __|    \\|     |");
  Serial.println("|    -|     | | | |  |  |  |  |  |  |");
  Serial.println("|__|__|__|__| |_| |_____|____/|_____|");
  Serial.println("https://paulwieland.github.io/ratgdo/");
  Serial.print("version ");
  Serial.print(VERSION);
  Serial.println("");

  delay(500);
}


/*************************** MAIN LOOP ***************************/
void loopRATGDO() {
  if (!setupComplete) {
    setupComplete = true;
    setupCompleteMillis = millis();

    if (OUTPUT_GDO != LED_BUILTIN) {
      digitalWrite(LED_BUILTIN, HIGH);
    }

    if (controlProtocol == "secplus2") {
      LittleFS.begin();

      readCounterFromFlash("idCode", idCode);
      Serial.print("exisiting client ID: ");
      Serial.println(idCode, HEX);
      if ((idCode & 0xFFF) != 0x539) {
        Serial.println("Initializing new client ID: ");
        idCode = (random(0x1, 0xFFFF) % 0x7FF) << 12 | 0x539;
        writeCounterToFlash("idCode", idCode);
        Serial.println(idCode, HEX);
      }
      readCounterFromFlash("rolling", rollingCodeCounter);

      Serial.println("Syncing rolling code counter after reboot...");
      sync(); // send reboot/sync to the opener on startup
    }

  }

  obstructionLoop();
  gdoStateLoop();
  dryContactLoop();
  statusUpdateLoop();
  wallPanelEmulatorLoop();
}