#include "rolling_store.h"

namespace ratgdo {
namespace {

uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void write32(uint8_t* p, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8 * i));
}

uint32_t checksum(const uint8_t* data) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < RecordSize - 4; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
  }
  return ~crc;
}

bool validId(uint32_t id) {
  return (id & 0xfff) == 0x539;
}

bool valid(const uint8_t* record) {
  return read32(record) == 0x5247444f && read32(record + 4) == 1 &&
         validId(read32(record + 8)) && read32(record + 12) <= RollingLimit &&
         read32(record + 16) != 0 &&
         read32(record + 20) == checksum(record);
}

} // namespace

bool RollingStore::fail(const char* error) {
  ready_ = false;
  error_ = error;
  return false;
}

bool RollingStore::begin() {
  ready_ = false;
  uint8_t record[RecordSize];
  const ReadResult result = storage_.read(record);
  if (result == ReadResult::Missing) {
    return fail(storage_.hasPending() ? "Interrupted provisioning; manual recovery required"
                                      : "Unprovisioned; explicit provisioning required");
  }
  if (result != ReadResult::Ok || !valid(record)) {
    return fail("Unreadable or corrupt controller record; manual recovery required");
  }
  id_ = read32(record + 8);
  next_ = end_ = read32(record + 12);
  generation_ = read32(record + 16);
  if (next_ == RollingLimit) return fail("Rolling codes exhausted; new identity required");
  ready_ = true;
  error_ = "";
  return true;
}

bool RollingStore::save(uint32_t id, uint32_t end, uint32_t generation) {
  uint8_t record[RecordSize];
  write32(record, 0x5247444f);
  write32(record + 4, 1);
  write32(record + 8, id);
  write32(record + 12, end);
  write32(record + 16, generation);
  write32(record + 20, checksum(record));
  if (!storage_.commit(record)) return fail("Controller record commit failed; transmission disabled");
  return true;
}

bool RollingStore::provision(uint32_t id, uint32_t next) {
  uint8_t record[RecordSize];
  if (storage_.read(record) != ReadResult::Missing || storage_.hasPending()) {
    return fail("Provision refused: controller files exist or storage is unreadable");
  }
  if (!validId(id) || next >= RollingLimit) {
    return fail("Invalid provision values: ID must end in 0x539; counter must be 28-bit");
  }
  if (!save(id, next, 1)) return false;
  return begin();
}

bool RollingStore::take(uint32_t& counter) {
  if (!ready_) return false;
  if (next_ >= RollingLimit) return fail("Rolling codes exhausted; new identity required");
  if (next_ == end_) {
    if (generation_ == UINT32_MAX) return fail("Record generation exhausted; manual recovery required");
    const uint32_t remaining = RollingLimit - end_;
    const uint32_t newEnd = end_ + (remaining < ReservationSize ? remaining : ReservationSize);
    if (!save(id_, newEnd, generation_ + 1)) return false;
    end_ = newEnd;
    ++generation_;
  }
  counter = next_++;
  return true;
}

} // namespace ratgdo
