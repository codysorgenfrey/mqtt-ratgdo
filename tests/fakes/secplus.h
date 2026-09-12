#ifndef TEST_SECPLUS_H
#define TEST_SECPLUS_H
#include <stdint.h>
int encode_wireline(uint32_t rolling, uint64_t fixed, uint32_t data, uint8_t* packet);
int decode_wireline(const uint8_t* packet, uint32_t* rolling, uint64_t* fixed, uint32_t* data);
#endif
