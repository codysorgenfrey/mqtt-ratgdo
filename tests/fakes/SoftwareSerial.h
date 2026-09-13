#ifndef TEST_SOFTWARE_SERIAL_H
#define TEST_SOFTWARE_SERIAL_H
#include "Arduino.h"
#include <deque>
constexpr int SWSERIAL_8E1 = 1, SWSERIAL_8N1 = 2;
class SoftwareSerial {
 public:
  std::vector<std::vector<byte>> sent;
  std::deque<byte> received;
  void begin(int, int, int, int, bool) {}
  size_t write(const byte* data, size_t size) {
    sent.emplace_back(data, data + size);
    return size;
  }
  int available() const { return int(received.size()); }
  int read() {
    if (received.empty()) return -1;
    const byte value = received.front();
    received.pop_front();
    return value;
  }
};
#endif
