#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>
#include "rolling_storage.h"

namespace {

const char* const StatePath = "/ratgdo.state";
const char* const PendingPath = "/ratgdo.tmp";

class LittleFSStorage : public ratgdo::RollingStorage {
 public:
  ratgdo::ReadResult read(uint8_t* record) override {
    if (!LittleFS.exists(StatePath)) return ratgdo::ReadResult::Missing;
    return readExact(StatePath, record) ? ratgdo::ReadResult::Ok : ratgdo::ReadResult::Error;
  }

  bool hasPending() override { return LittleFS.exists(PendingPath); }

  bool commit(const uint8_t* record) override {
    File file = LittleFS.open(PendingPath, "w");
    if (!file) return false;
    const bool written = file.write(record, ratgdo::RecordSize) == ratgdo::RecordSize;
    file.flush();
    const bool writeError = file.getWriteError() != 0;
    file.close();
    // ESP8266 flush/close return void. Reopen after closing to verify the
    // committed file, not the writer's cache, before the atomic rename.
    uint8_t check[ratgdo::RecordSize];
    if (!written || writeError || !readExact(PendingPath, check) ||
        memcmp(record, check, sizeof(check)) != 0) return false;
    if (!LittleFS.rename(PendingPath, StatePath)) return false;
    return readExact(StatePath, check) && memcmp(record, check, sizeof(check)) == 0;
  }

 private:
  bool readExact(const char* path, uint8_t* record) {
    File file = LittleFS.open(path, "r");
    if (!file) return false;
    const bool ok = file.size() == ratgdo::RecordSize &&
                    file.read(record, ratgdo::RecordSize) == int(ratgdo::RecordSize);
    file.close();
    return ok;
  }
};

LittleFSStorage storage;
ratgdo::RollingStore store(storage);
bool mounted = false;

bool mountStorage() {
  if (mounted) return true;
#if defined(ESP8266)
  FSInfo info;
  if (LittleFS.info(info)) {
    mounted = true;
    return true;
  }
  LittleFSConfig config;
  config.setAutoFormat(false);
  if (!LittleFS.setConfig(config)) return store.fail("LittleFS configuration failed");
  mounted = LittleFS.begin();
#elif defined(ESP32)
  mounted = LittleFS.begin(false);
#else
#error RATGDO persistence requires ESP8266 or ESP32 LittleFS
#endif
  if (!mounted) return store.fail("LittleFS mount failed; filesystem was NOT formatted");
  return true;
}

} // namespace

ratgdo::RollingStore& ratgdoRollingStore() { return store; }

bool beginRollingStorage() {
  if (store.ready()) return true;
  return mountStorage() && store.begin();
}

bool provisionRATGDO(uint32_t controllerId, uint32_t nextRollingCode) {
  const bool ok = mountStorage() && store.provision(controllerId, nextRollingCode);
  if (!ok) {
    Serial.print("RATGDO storage: ");
    Serial.println(store.error());
  }
  return ok;
}

bool ratgdoStorageReady() { return store.ready(); }
const char* ratgdoStorageError() { return store.error(); }
