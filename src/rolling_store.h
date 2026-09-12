#ifndef RATGDO_ROLLING_STORE_H
#define RATGDO_ROLLING_STORE_H

#include <stddef.h>
#include <stdint.h>

namespace ratgdo {

constexpr uint32_t RollingLimit = 0x10000000; // Exclusive 28-bit limit; never wrap.
constexpr uint32_t ReservationSize = 64;
constexpr size_t RecordSize = 24;

enum class ReadResult { Ok, Missing, Error };

class RollingStorage {
 public:
  virtual ~RollingStorage() {}
  virtual ReadResult read(uint8_t* record) = 0;
  virtual bool hasPending() = 0;
  // Success means the entire record is durably, atomically committed.
  virtual bool commit(const uint8_t* record) = 0;
};

class RollingStore {
 public:
  explicit RollingStore(RollingStorage& storage) : storage_(storage) {}
  bool begin();
  bool provision(uint32_t id, uint32_t next);
  bool take(uint32_t& counter);
  bool ready() const { return ready_; }
  const char* error() const { return error_; }
  uint32_t id() const { return id_; }
  uint32_t next() const { return next_; }
  bool fail(const char* error);

 private:
  bool save(uint32_t id, uint32_t end, uint32_t generation);
  RollingStorage& storage_;
  bool ready_ = false;
  const char* error_ = "Storage not initialized";
  uint32_t id_ = 0;
  uint32_t next_ = 0;
  uint32_t end_ = 0;
  uint32_t generation_ = 0;
};

} // namespace ratgdo
#endif
