#ifndef TEST_SOFTWARE_SERIAL_H
#define TEST_SOFTWARE_SERIAL_H
#include "Arduino.h"
constexpr int SWSERIAL_8E1 = 1, SWSERIAL_8N1 = 2;
class SoftwareSerial {
 public:
  std::vector<std::vector<byte>> sent;
  void begin(int, int, int, int, bool) {}
  size_t write(const byte* data, size_t size) {
    sent.emplace_back(data, data + size);
    return size;
  }
  int available() const { return 0; }
  int read() const { return -1; }
};
#endif
