#include "ratgdo.h"
#include "rolling_code.h"
#include <LittleFS.h>
#include <secplus.h>
#include <array>
#include <assert.h>
#include <stdio.h>

FakeSerial Serial;
FakeFS LittleFS;
unsigned fakeMillis = 0;
unsigned txPulses = 0;
extern void gdoStateLoop();
extern uint8_t doorState, lightState, lockState, motionState, obstructionState;

using State = std::array<uint8_t, 5>;

State state() {
  return {{doorState, lightState, lockState, motionState, obstructionState}};
}

void receive(const byte* data, size_t size, unsigned now) {
  fakeMillis = now;
  swSerial.received.insert(swSerial.received.end(), data, data + size);
  while (swSerial.available()) gdoStateLoop();
}

int main() {
  controlProtocol = "secplus2";
  doorState = 0;
  lightState = 2;
  lockState = 2;
  motionState = 1;
  obstructionState = 2;
  lastRX = 0;
  const State initial = state();

  byte status[SECPLUS2_CODE_LEN];
  assert(encode_wireline(42, 0x123539, 0x03000281, status) == 0);
  uint32_t rolling, data;
  uint64_t fixed;
  assert(decode_wireline(status, &rolling, &fixed, &data) == 0);
  assert(rolling == 42 && fixed == 0x123539);

  // Valid framing alone is insufficient: reserved bits invalidate the payload.
  byte malformed[SECPLUS2_CODE_LEN];
  memcpy(malformed, status, sizeof(malformed));
  malformed[4] |= 0xc0;
  assert(decode_wireline(malformed, &rolling, &fixed, &data) != 0);
  receive(malformed, sizeof(malformed), 100);
  assert(lastRX == 0 && state() == initial);
  assert(Serial.messages.find("RATGDO: rolling code decoding failed") != std::string::npos);

  receive(status, sizeof(status) - 1, 200);
  assert(lastRX == 0 && state() == initial);
  receive(status + sizeof(status) - 1, 1, 201);
  assert(lastRX == 201);
  const State expected = {{2, 1, 1, 0, 2}};
  assert(state() == expected);

  // An unchanged status still proves the receive path is alive.
  receive(status, sizeof(status), 300);
  assert(lastRX == 300 && state() == expected);
  receive(malformed, sizeof(malformed), 400);
  assert(lastRX == 300 && state() == expected);
  assert(!readRollingCode(malformed, doorState, lightState, lockState, motionState, obstructionState));
  assert(state() == expected);

  // Cover a late decoder rejection too, after both halves can be decoded.
  bool rejectedLate = false;
  for (size_t i = 5; i < sizeof(status) && !rejectedLate; ++i) {
    for (unsigned bit = 0; bit < 8 && !rejectedLate; ++bit) {
      memcpy(malformed, status, sizeof(malformed));
      malformed[i] ^= byte(1 << bit);
      data = UINT32_MAX;
      if (decode_wireline(malformed, &rolling, &fixed, &data) != 0 &&
          data != UINT32_MAX && (data & 0xff) == 0x81 &&
          ((data >> 8) & 0xf) != expected[0]) {
        receive(malformed, sizeof(malformed), 450);
        assert(lastRX == 300 && state() == expected);
        rejectedLate = true;
      }
    }
  }
  assert(rejectedLate);
  receive(status, sizeof(status), 500);
  assert(lastRX == 500 && state() == expected);
  assert(readRollingCode(status, doorState, lightState, lockState, motionState, obstructionState));

  // Freshness is global, including decoded commands that update no known field.
  byte query[SECPLUS2_CODE_LEN];
  assert(encode_wireline(43, 0x123539, 0x80, query) == 0);
  receive(query, sizeof(query), 600);
  assert(lastRX == 600 && state() == expected);
  const byte noise[] = {0xff, 0xfe, 0xfd};
  receive(noise, sizeof(noise), 700);
  assert(lastRX == 600 && state() == expected);
  fakeMillis = 800;
  gdoStateLoop();
  assert(lastRX == 600);

  // SP1 retains its existing header-time timestamp for transmit spacing.
  controlProtocol = "secplus1";
  const byte header = 0x38;
  receive(&header, 1, 900);
  assert(lastRX == 900);
  const byte value = 0x05;
  receive(&value, 1, 901);
  assert(lastRX == 900);

  assert(swSerial.sent.empty() && txPulses == 0 && LittleFS.commits == 0);
  puts("receive: real codec, complete valid frames, unchanged status, invalid frames, SP1 timing passed");
}
