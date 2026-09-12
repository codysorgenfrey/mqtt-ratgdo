#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <functional>
#include <string>
#include <vector>
using byte = uint8_t;
using String = std::string;
constexpr int D0 = 0, D1 = 1, D2 = 2, D3 = 3, D5 = 5, D6 = 6, D7 = 7, D8 = 8;
constexpr int LED_BUILTIN = 4, HIGH = 1, LOW = 0, INPUT_PULLUP = 2;
constexpr int OUTPUT = 1, INPUT = 0, CHANGE = 3, HEX = 16, BIN = 2;
#define IRAM_ATTR
#define bitRead(value, bit) (((value) >> (bit)) & 1)
struct FakeSerial {
  std::string messages;
  operator bool() const { return true; }
  void begin(unsigned) {}
  void print(const char* text) { messages += text; }
  template <typename T> void print(const T&) {}
  template <typename T> void print(const T&, int) {}
  void println(const char* text) { messages += text; messages += '\n'; }
  template <typename T> void println(const T&) {}
  template <typename T> void println(const T&, int) {}
};
extern FakeSerial Serial;
extern unsigned fakeMillis;
extern unsigned txPulses;
inline unsigned millis() { return fakeMillis; }
inline void delay(unsigned ms) { fakeMillis += ms; }
inline void delayMicroseconds(unsigned) {}
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) { if (pin == D1 && value == HIGH) ++txPulses; }
inline int digitalRead(int) { return HIGH; }
inline void attachInterrupt(int, void (*)(), int) {}
#endif
