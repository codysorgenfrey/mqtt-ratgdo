#include "rolling_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>

using namespace ratgdo;

struct MemoryStorage : RollingStorage {
  uint8_t record[RecordSize] = {};
  bool exists = false;
  bool pending = false;
  bool readError = false;
  int commits = 0;
  enum Failure { None, BeforeWrite, TornWrite, BeforeRename, AfterRename } failure = None;

  ReadResult read(uint8_t* data) override {
    if (readError) return ReadResult::Error;
    if (!exists) return ReadResult::Missing;
    memcpy(data, record, RecordSize);
    return ReadResult::Ok;
  }
  bool hasPending() override { return pending; }
  bool commit(const uint8_t* data) override {
    ++commits;
    if (failure == BeforeWrite) return false;
    pending = true;
    if (failure == TornWrite || failure == BeforeRename) return false;
    memcpy(record, data, RecordSize);
    exists = true;
    pending = false;
    return failure != AfterRename;
  }
};

void rebootAndWear() {
  MemoryStorage fs;
  RollingStore store(fs);
  uint32_t counter = 999;
  assert(!store.begin() && !store.take(counter) && counter == 999);
  assert(store.provision(0x123539, 10));
  assert(fs.commits == 1);
  assert(store.take(counter) && counter == 10);
  assert(fs.commits == 2);
  for (unsigned i = 1; i < ReservationSize; ++i) {
    assert(store.take(counter) && counter == 10 + i);
  }
  assert(fs.commits == 2);
  assert(store.take(counter) && counter == 10 + ReservationSize);
  assert(fs.commits == 3);
  RollingStore reboot(fs);
  assert(reboot.begin() && reboot.id() == 0x123539);
  assert(reboot.take(counter) && counter == 10 + 2 * ReservationSize);
  // Even a reset immediately after reservation burns the rest of the range.
  for (unsigned i = 0; i < 100; ++i) {
    const uint32_t last = counter;
    RollingStore nextBoot(fs);
    assert(nextBoot.begin() && nextBoot.take(counter) && counter > last);
  }
}

void failures() {
  for (auto failure : {MemoryStorage::BeforeWrite, MemoryStorage::TornWrite,
                       MemoryStorage::BeforeRename, MemoryStorage::AfterRename}) {
    MemoryStorage fs;
    RollingStore store(fs);
    assert(store.provision(0x123539, 0));
    fs.failure = failure;
    uint32_t counter = 999;
    assert(!store.take(counter) && counter == 999 && !store.ready());
    assert(!store.take(counter));
    fs.failure = MemoryStorage::None;
    RollingStore reboot(fs);
    assert(reboot.begin());
    assert(reboot.take(counter));
    assert(counter == (failure == MemoryStorage::AfterRename ? ReservationSize : 0));
  }

  for (auto failure : {MemoryStorage::TornWrite, MemoryStorage::BeforeRename}) {
    MemoryStorage fs;
    fs.failure = failure;
    RollingStore store(fs);
    assert(!store.provision(0x123539, 0));
    fs.failure = MemoryStorage::None;
    RollingStore reboot(fs);
    assert(!reboot.begin() && !reboot.provision(0x234539, 0));
  }
}

void corruptAndProvision() {
  MemoryStorage fs;
  RollingStore store(fs);
  assert(!store.provision(0, 0));
  assert(!store.provision(0x123539, RollingLimit));
  assert(!fs.exists && fs.commits == 0);
  assert(store.provision(0x123539, 42));
  assert(!store.provision(0x234539, 0));
  const MemoryStorage original = fs;
  for (size_t byte = 0; byte < RecordSize; ++byte) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      fs = original;
      fs.record[byte] ^= 1 << bit;
      RollingStore reboot(fs);
      assert(!reboot.begin() && !reboot.provision(0x234539, 0));
    }
  }
  fs = original;
  fs.readError = true;
  RollingStore unreadable(fs);
  assert(!unreadable.begin() && !unreadable.provision(0x234539, 0));
}

void exhaustion() {
  MemoryStorage fs;
  RollingStore store(fs);
  assert(store.provision(0x123539, RollingLimit - 2));
  uint32_t counter;
  assert(store.take(counter) && counter == RollingLimit - 2);
  assert(store.take(counter) && counter == RollingLimit - 1);
  assert(!store.take(counter) && counter == RollingLimit - 1);
  RollingStore reboot(fs);
  assert(!reboot.begin() && !reboot.take(counter));
  assert(!reboot.provision(0x123539, 0));
}

int main() {
  rebootAndWear();
  failures();
  corruptAndProvision();
  exhaustion();
  puts("rolling store: reboot, wear, torn writes, corruption, provisioning, exhaustion passed");
}
