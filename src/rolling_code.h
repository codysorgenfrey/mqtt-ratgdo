#ifndef _RATGDO_ROLLING_CODE_H
#define _RATGDO_ROLLING_CODE_H

#include "common.h"

void readRollingCode(byte rxSP2RollingCode[SECPLUS2_CODE_LEN], uint8_t &door, uint8_t &light, uint8_t &lock, uint8_t &motion, uint8_t &obstruction);
bool getRollingCode(const char *command);
bool consumeRollingCode(const byte* payload, unsigned int length);
void printRollingCode(byte code[SECPLUS2_CODE_LEN]);

#endif