#include "ratgdo.h"
#include "rolling_code.h"
#include "rolling_storage.h"
#include <LittleFS.h>
#include <assert.h>
#include <stdio.h>

FakeSerial Serial;
FakeFS LittleFS;
unsigned fakeMillis = 0;
unsigned txPulses = 0;
bool encodingFails = false;
extern void transmit(byte*, unsigned);
extern uint8_t motionState;

// Record actual protocol inputs without requiring an Arduino codec on the host.
extern "C" int encode_wireline(uint32_t rolling, uint64_t fixed, uint32_t data, uint8_t* packet) {
  if (encodingFails) return -1;
  memset(packet, 0, SECPLUS2_CODE_LEN);
  memcpy(packet, &rolling, 4);
  memcpy(packet + 4, &fixed, 8);
  memcpy(packet + 12, &data, 4);
  return 0;
}
extern "C" int decode_wireline(const uint8_t*, uint32_t*, uint64_t*, uint32_t*) { return -1; }

uint32_t counter(size_t index) {
  uint32_t result;
  memcpy(&result, swSerial.sent.at(index).data(), 4);
  return result;
}

void blockedCommands() {
  const auto count = swSerial.sent.size();
  const auto pulses = txPulses;
  sync();
  toggleDoor();
  openDoor();
  closeDoor();
  stopDoor();
  toggleLight();
  toggleLock();
  transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
  dryContactDoorOpen = true;
  dryContactDoorClose = true;
  dryContactToggleLight = true;
  motionState = 1;
  loopRATGDO();
  assert(swSerial.sent.size() == count && txPulses == pulses);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  // Parent-owned files must survive all operations untouched.
  LittleFS.files["/hkc0"] = {1, 2, 3};
  if (mode == "mount") {
    LittleFS.mountFails = true;
    assert(!setupRATGDO() && !LittleFS.autoFormat);
    assert(!provisionRATGDO(0x123539, 0));
    blockedCommands();
  } else if (mode == "blank") {
    assert(!setupRATGDO() && !ratgdoStorageReady());
    blockedCommands();
    assert(LittleFS.commits == 0);
  } else {
    // Check the already-mounted shared-filesystem path, too.
    LittleFS.mounted = true;
    assert(provisionRATGDO(0x123539, mode == "exhaustion" ? ratgdo::RollingLimit - 1 : 0));
    assert(swSerial.sent.empty() && txPulses == 0);
    assert(setupRATGDO());
    if (mode == "normal") {
      loopRATGDO();
      assert(swSerial.sent.size() == 6 && LittleFS.commits == 2);
      const uint32_t bootData[] = {0x8b, 0x80, 0xa0, 0x80, 0x92, 0x92};
      for (size_t i = 0; i < 6; ++i) {
        uint32_t data;
        memcpy(&data, swSerial.sent[i].data() + 12, 4);
        assert(data == bootData[i] && counter(i) == i);
      }
      toggleDoor();
      assert(swSerial.sent.size() == 8 && counter(6) == 6 && counter(7) == 6);
      toggleLight();
      toggleLock();
      assert(counter(8) == 7 && counter(9) == 8);
      motionState = 1;
      loopRATGDO();
      assert(counter(10) == 9);
      idCode = 0x999539;
      rollingCodeCounter = 0;
      toggleLight();
      uint64_t fixed;
      memcpy(&fixed, swSerial.sent[11].data() + 4, 8);
      assert(uint32_t(fixed) == 0x123539 && counter(11) == 10);
      const auto count = swSerial.sent.size();
      transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
      assert(!getRollingCode("door2"));
      assert(getRollingCode("light"));
      assert(!getRollingCode("invalid"));
      transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
      assert(getRollingCode("light"));
      txSP2RollingCode[0] ^= 1;
      transmit(txSP2RollingCode, SECPLUS2_CODE_LEN);
      encodingFails = true;
      toggleDoor();
      assert(swSerial.sent.size() == count);
      encodingFails = false;
      assert(getRollingCode("door1"));
      assert(!getRollingCode("door2")); // No release before a transmitted press.
      assert(!getRollingCode(nullptr));
    } else if (mode == "exhaustion") {
      toggleDoor();
      assert(swSerial.sent.size() == 2);
      assert(counter(0) == ratgdo::RollingLimit - 1 && counter(1) == counter(0));
      toggleLight();
      assert(!ratgdoStorageReady());
      blockedCommands();
    } else if (mode == "corrupt") {
      LittleFS.files["/ratgdo.state"][8] ^= 1;
      assert(!ratgdoRollingStore().begin());
      assert(!provisionRATGDO(0x234539, 0));
      blockedCommands();
    } else if (mode == "truncated") {
      LittleFS.files["/ratgdo.state"].pop_back();
      assert(!ratgdoRollingStore().begin());
      blockedCommands();
    } else if (mode == "pending") {
      LittleFS.files["/ratgdo.tmp"] = {1, 2, 3};
      assert(ratgdoRollingStore().begin());
      loopRATGDO();
      assert(swSerial.sent.size() == 6);
      assert(!LittleFS.exists("/ratgdo.tmp"));
    } else if (mode == "interrupted") {
      LittleFS.files["/ratgdo.tmp"] = LittleFS.files["/ratgdo.state"];
      LittleFS.files.erase("/ratgdo.state");
      assert(!ratgdoRollingStore().begin());
      assert(!provisionRATGDO(0x234539, 0));
      blockedCommands();
    } else {
      if (mode == "open") LittleFS.failure = FakeFS::OpenWrite;
      else if (mode == "short") LittleFS.failure = FakeFS::ShortWrite;
      else if (mode == "flush") LittleFS.failure = FakeFS::Flush;
      else if (mode == "rename") LittleFS.failure = FakeFS::Rename;
      else if (mode == "readback") LittleFS.failure = FakeFS::ReadBack;
      else assert(false);
      loopRATGDO();
      assert(!ratgdoStorageReady());
      blockedCommands();
    }
  }
  assert(!Serial.messages.empty());
  assert((LittleFS.files["/hkc0"] == std::vector<uint8_t>{1, 2, 3}));
  printf("transmission/storage integration: %s passed\n", argv[1]);
}
