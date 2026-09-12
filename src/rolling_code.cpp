#include <Arduino.h>
#include <LittleFS.h>
#include "rolling_code.h"
#include "rolling_storage.h"

extern "C" {
  #include "secplus.h"
}

namespace {
bool prepared = false;
bool preparedDoorPress = false;
bool doorReleasePending = false;
uint32_t doorCounter = 0;
byte preparedPayload[SECPLUS2_CODE_LEN];
}

bool consumeRollingCode(const byte* payload, unsigned int length) {
  const bool allowed = ratgdoRollingStore().ready() && prepared &&
      length == SECPLUS2_CODE_LEN && payload != nullptr &&
      memcmp(payload, preparedPayload, SECPLUS2_CODE_LEN) == 0;
  prepared = false;
  doorReleasePending = allowed && preparedDoorPress;
  if (!allowed) Serial.println("RATGDO: blocked unreserved or stale transmission");
  return allowed;
}

void readRollingCode(byte rxSP2RollingCode[SECPLUS2_CODE_LEN], uint8_t &door, uint8_t &light, uint8_t &lock, uint8_t &motion, uint8_t &obstruction){
	uint32_t rolling = 0;
	uint64_t fixed = 0;
	uint32_t data = 0;

	uint16_t cmd = 0;
	uint8_t nibble = 0;
	uint8_t byte1 = 0;
	uint8_t byte2 = 0;

	decode_wireline(rxSP2RollingCode, &rolling, &fixed, &data);

	cmd = ((fixed >> 24) & 0xf00) | (data & 0xff);

	nibble = (data >> 8) & 0xf;
	byte1 = (data >> 16) & 0xff;
	byte2 = (data >> 24) & 0xff;

	printRollingCode(rxSP2RollingCode);

	if(cmd == 0x81){
		door = nibble;
		light = (byte2 >> 1) & 1;
		lock = byte2 & 1;
		motion = 0; // when the status message is read, reset motion state to 0|clear
		// obstruction = (byte1 >> 6) & 1; // unreliable due to the time it takes to register an obstruction

		Serial.print(" | STATUS:");
		Serial.print(" door:");
		Serial.print(nibble);
		Serial.print(" light:");
		Serial.print((byte2 >> 1) & 1);
		Serial.print(" lock:");
		Serial.print((byte2 & 1));
		Serial.print(" obs:");
		Serial.print((byte1 >> 6) & 1);

	}else if(cmd == 0x281){
		light ^= 1; // toggle bit

		Serial.print(" | LIGHT:");
		Serial.print(light);
	}else if(cmd == 0x84){
	}else if(cmd == 0x285){
		motion = 1; // toggle bit
		Serial.print(" | MOTION:");
		Serial.print(motion);
	}

	Serial.println("");
}

bool getRollingCode(const char *command){
  prepared = false;
  const bool release = command && strcmp(command, "door2") == 0;
  const bool pairedRelease = release && doorReleasePending;
  doorReleasePending = false;
  if (!ratgdoRollingStore().ready()) {
    Serial.print("RATGDO storage: ");
    Serial.println(ratgdoRollingStore().error());
    return false;
  }
  if (!command || (release && !pairedRelease)) {
    Serial.println("ERROR: Invalid command or unpaired door release");
    return false;
  }
  idCode = ratgdoRollingStore().id();
	Serial.print("rolling code for ");
	Serial.print(idCode, HEX);
	Serial.print(" ");
	Serial.print(rollingCodeCounter);
	Serial.print("|");
	Serial.print(command);
	Serial.print(" : ");

	uint64_t id = idCode;
	uint64_t fixed = 0;
	uint32_t data = 0;

	if(strcmp(command,"reboot1") == 0){
		fixed = 0x400000000;
		data = 0x0000008b;
	}else if(strcmp(command,"reboot2") == 0){
		fixed = 0;
		data = 0x00000080;
	}else if(strcmp(command,"reboot3") == 0){
		fixed = 0;
		data = 0x000000a0;
	}else if(strcmp(command,"reboot4") == 0){
		fixed = 0;
		data = 0x00000080;
	}else if(strcmp(command,"reboot5") == 0){
		fixed = 0x300000000;
		data = 0x00000092;
	}else if(strcmp(command,"reboot6") == 0){
		fixed = 0x300000000;
		data = 0x00000092;
	}else if(strcmp(command,"door1") == 0){
		fixed = 0x200000000;
		data = 0x01010280;
	}else if(strcmp(command,"door2") == 0){
		fixed = 0x200000000;
		data = 0x01000280;
	}else if(strcmp(command,"light") == 0){
		fixed = 0x200000000;
		data = 0x00000281;
	}else if(strcmp(command,"lock") == 0){
		fixed = 0x0100000000;
		data = 0x0000028c;
	}else{
		Serial.println("ERROR: Invalid command");
		return false;
	}

	fixed = fixed | id;

  uint32_t counter = doorCounter;
  if (!pairedRelease && !ratgdoRollingStore().take(counter)) {
    Serial.print("RATGDO storage: ");
    Serial.println(ratgdoRollingStore().error());
    return false;
  }
  rollingCodeCounter = ratgdoRollingStore().next();
  if (encode_wireline(counter, fixed, data, txSP2RollingCode) != 0) {
    Serial.println("RATGDO: rolling code encoding failed");
    return false;
  }
  preparedDoorPress = strcmp(command, "door1") == 0;
  if (preparedDoorPress) doorCounter = counter;
  memcpy(preparedPayload, txSP2RollingCode, SECPLUS2_CODE_LEN);
  prepared = true;

	printRollingCode(txSP2RollingCode);
	Serial.println("");

  // The protocol's press/release pair shares one reserved counter, only in RAM.
  return true;
}

void printRollingCode(byte code[SECPLUS2_CODE_LEN]){
	for(int i = 0; i < SECPLUS2_CODE_LEN; i++){
		if(code[i] <= 0x0f) Serial.print("0");
		Serial.print(code[i],HEX);
	}
}