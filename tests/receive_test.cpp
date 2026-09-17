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

void diagnostic(uint32_t uptimeMs, uint16_t command, uint8_t value,
                uint8_t light, RatgdoRxKind kind) {
  RatgdoRxDiagnostic record;
  assert(readRatgdoRxDiagnostic(record));
  assert(record.uptimeMs == uptimeMs && record.command == command &&
         record.value == value && record.light == light && record.kind == kind);
  assert(!readRatgdoRxDiagnostic(record));
}

void lightMessages() {
  unsigned now = 1000;
  setRatgdoRxDiagnosticsEnabled(true);
  for (uint8_t initialLight = 0; initialLight <= 2; ++initialLight) {
    for (uint8_t action = 0; action <= 15; ++action) {
      doorState = 4;
      lightState = initialLight;
      lockState = 1;
      motionState = 1;
      obstructionState = 0;
      const State before = state();
      State expected = before;
      if (action < 2) expected[1] = action;
      else if (action == 2 && initialLight < 2) expected[1] = 1 - initialLight;

      byte packet[SECPLUS2_CODE_LEN];
      // Nonzero unused bytes and codec-generated parity must not alter the action.
      assert(encode_wireline(44, 0x200123539ULL,
                             0xa55a0081U | (uint32_t(action) << 8), packet) == 0);
      const unsigned long previousRX = lastRX;
      Serial.messages.clear();
      assert(readRollingCode(packet, doorState, lightState, lockState, motionState, obstructionState));
      assert(state() == expected && lastRX == previousRX);
      diagnostic(fakeMillis, 0x281, action, expected[1], RatgdoRxKind::Light);
      assert(Serial.messages.find("LIGHT action:") != std::string::npos);
      if (action > 2) {
        assert(Serial.messages.find("unsupported action; state unchanged") != std::string::npos);
      } else if (action == 2 && initialLight == 2) {
        assert(Serial.messages.find("toggle without known state; state unchanged") != std::string::npos);
      }

      lightState = initialLight;
      receive(packet, sizeof(packet), ++now);
      assert(state() == expected && lastRX == now);
      diagnostic(now, 0x281, action, expected[1], RatgdoRxKind::Light);
      // Explicit ON/OFF is idempotent; a repeated toggle still toggles known state.
      if (action == 2 && initialLight < 2) expected[1] = initialLight;
      receive(packet, sizeof(packet), ++now);
      assert(state() == expected && lastRX == now);
      diagnostic(now, 0x281, action, expected[1], RatgdoRxKind::Light);
    }
  }
  setRatgdoRxDiagnosticsEnabled(false);

  // A partial or rejected LIGHT frame cannot modify state or refresh lastRX.
  byte packet[SECPLUS2_CODE_LEN];
  assert(encode_wireline(45, 0x200123539ULL, 0x181, packet) == 0);
  lightState = 0;
  const State before = state();
  const unsigned long previousRX = lastRX;
  receive(packet, sizeof(packet) - 1, ++now);
  assert(state() == before && lastRX == previousRX);
  receive(packet + sizeof(packet) - 1, 1, ++now);
  State expected = before;
  expected[1] = 1;
  assert(state() == expected && lastRX == now);
  packet[4] |= 0xc0;
  Serial.messages.clear();
  assert(!readRollingCode(packet, doorState, lightState, lockState, motionState, obstructionState));
  assert(state() == expected && lastRX == now);
  receive(packet, sizeof(packet), ++now);
  assert(state() == expected && lastRX == now - 1);
  assert(Serial.messages.find("rolling code decoding failed") != std::string::npos);

  // STATUS remains authoritative after any prior light state.
  for (uint8_t initialLight = 0; initialLight <= 2; ++initialLight) {
    for (uint8_t statusLight = 0; statusLight <= 1; ++statusLight) {
      lightState = initialLight;
      motionState = 1;
      obstructionState = 2;
      assert(encode_wireline(46, 0x123539,
                             (uint32_t(statusLight) << 25) | 0x01000281, packet) == 0);
      receive(packet, sizeof(packet), ++now);
      const State reconciled = {{2, statusLight, 1, 0, 2}};
      assert(state() == reconciled && lastRX == now);
      assert(encode_wireline(47, 0x200123539ULL, 0x281, packet) == 0);
      receive(packet, sizeof(packet), ++now);
      expected = reconciled;
      expected[1] = 1 - statusLight;
      assert(state() == expected && lastRX == now);
    }
  }
}

void diagnosticMessages() {
  byte packet[SECPLUS2_CODE_LEN];
  assert(encode_wireline(48, 0x123539, 0x03000281, packet) == 0);
  receive(packet, sizeof(packet), 3000);
  RatgdoRxDiagnostic record = {123, 456, 7, 8, RatgdoRxKind::Command};
  assert(!readRatgdoRxDiagnostic(record) && ratgdoRxDiagnosticsDropped() == 0);
  assert(record.uptimeMs == 123 && record.command == 456 && record.value == 7 &&
         record.light == 8 && record.kind == RatgdoRxKind::Command);

  setRatgdoRxDiagnosticsEnabled(true);
  const State before = state();
  // Both unchanged statuses must be captured, not just state transitions.
  for (unsigned now = 3001; now <= 3002; ++now) {
    receive(packet, sizeof(packet), now);
    assert(state() == before && lastRX == now);
    diagnostic(now, 0x81, 1, 1, RatgdoRxKind::Status);
  }
  assert(encode_wireline(49, 0x123539, 0x01000281, packet) == 0);
  receive(packet, sizeof(packet), 3003);
  diagnostic(3003, 0x81, 0, 0, RatgdoRxKind::Status);
  assert(encode_wireline(50, 0xa00123539ULL, 0xbc, packet) == 0);
  receive(packet, sizeof(packet), 3004);
  diagnostic(3004, 0xabc, 0xff, 0, RatgdoRxKind::Command);
  packet[4] |= 0xc0;
  const State valid = state();
  receive(packet, sizeof(packet), 3005);
  assert(state() == valid && lastRX == 3004);
  diagnostic(3005, 0xffff, 0xff, 0, RatgdoRxKind::DecodeFailure);

  assert(encode_wireline(51, 0x123539, 0x80, packet) == 0);
  receive(packet, sizeof(packet) - 1, 3006);
  assert(!readRatgdoRxDiagnostic(record) && lastRX == 3004);
  receive(packet + sizeof(packet) - 1, 1, 3007);
  diagnostic(3007, 0x80, 0xff, 0, RatgdoRxKind::Command);

  // Drop newest on overflow, preserve FIFO order, then reuse drained slots.
  for (unsigned i = 0; i < RATGDO_RX_DIAGNOSTIC_CAPACITY + 3; ++i) {
    receive(packet, sizeof(packet), 3100 + i);
  }
  assert(ratgdoRxDiagnosticsDropped() == 3);
  for (unsigned i = 0; i < 8; ++i) {
    assert(readRatgdoRxDiagnostic(record) && record.uptimeMs == 3100 + i);
  }
  for (unsigned i = 0; i < 8; ++i) receive(packet, sizeof(packet), 3200 + i);
  for (unsigned i = 8; i < RATGDO_RX_DIAGNOSTIC_CAPACITY; ++i) {
    assert(readRatgdoRxDiagnostic(record) && record.uptimeMs == 3100 + i);
  }
  for (unsigned i = 0; i < 8; ++i) {
    assert(readRatgdoRxDiagnostic(record) && record.uptimeMs == 3200 + i);
  }
  assert(!readRatgdoRxDiagnostic(record) && ratgdoRxDiagnosticsDropped() == 3);
  receive(packet, sizeof(packet), 3300);
  setRatgdoRxDiagnosticsEnabled(true);
  assert(!readRatgdoRxDiagnostic(record) && ratgdoRxDiagnosticsDropped() == 0);
  receive(packet, sizeof(packet), UINT32_MAX);
  diagnostic(UINT32_MAX, 0x80, 0xff, 0, RatgdoRxKind::Command);
  receive(packet, sizeof(packet), 0);
  diagnostic(0, 0x80, 0xff, 0, RatgdoRxKind::Command);
  for (unsigned i = 0; i < RATGDO_RX_DIAGNOSTIC_CAPACITY + 1; ++i) {
    if (i == RATGDO_RX_DIAGNOSTIC_CAPACITY) {
      assert(encode_wireline(52, 0x200123539ULL, 0x181, packet) == 0);
    }
    receive(packet, sizeof(packet), 3400 + i);
  }
  assert(ratgdoRxDiagnosticsDropped() == 1);
  assert(lightState == 1 && lastRX == 3400 + RATGDO_RX_DIAGNOSTIC_CAPACITY);
  packet[4] |= 0xc0;
  receive(packet, sizeof(packet), 3499);
  assert(ratgdoRxDiagnosticsDropped() == 2);
  assert(lightState == 1 && lastRX == 3400 + RATGDO_RX_DIAGNOSTIC_CAPACITY);
  setRatgdoRxDiagnosticsEnabled(false);
  for (unsigned i = 0; i < RATGDO_RX_DIAGNOSTIC_CAPACITY + 1; ++i) {
    receive(packet, sizeof(packet), 3500 + i);
  }
  assert(!readRatgdoRxDiagnostic(record) && ratgdoRxDiagnosticsDropped() == 0);
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

  RatgdoRxDiagnostic record;
  assert(!readRatgdoRxDiagnostic(record) && ratgdoRxDiagnosticsDropped() == 0);
  lightMessages();
  diagnosticMessages();

  // SP1 retains its existing header-time timestamp for transmit spacing.
  controlProtocol = "secplus1";
  const byte header = 0x38;
  setRatgdoRxDiagnosticsEnabled(true);
  receive(&header, 1, 4000);
  assert(lastRX == 4000);
  const byte value = 0x05;
  receive(&value, 1, 4001);
  assert(lastRX == 4000);
  assert(!readRatgdoRxDiagnostic(record));

  assert(swSerial.sent.empty() && txPulses == 0 && LittleFS.commits == 0);
  puts("receive: real codec, LIGHT actions, STATUS reconciliation, invalid/partial frames, liveness, SP1 timing, bounded opt-in diagnostics passed");
}
