#ifndef TEST_LITTLEFS_H
#define TEST_LITTLEFS_H
#include "Arduino.h"
#include <map>
#include <memory>

struct FSInfo {};
struct LittleFSConfig {
  bool autoFormat = true;
  void setAutoFormat(bool enabled) { autoFormat = enabled; }
};

class File;
struct FakeFS {
  enum Failure { None, OpenWrite, ShortWrite, Flush, Rename, ReadBack } failure = None;
  bool mounted = false;
  bool mountFails = false;
  bool autoFormat = true;
  unsigned commits = 0;
  std::map<std::string, std::vector<uint8_t>> files;
  bool info(FSInfo&) const { return mounted; }
  bool setConfig(const LittleFSConfig& config) {
    if (mounted) return false;
    autoFormat = config.autoFormat;
    return true;
  }
  bool begin() { mounted = !mountFails; return mounted; }
  bool exists(const char* path) const { return files.count(path) != 0; }
  File open(const char* path, const char* mode);
  bool rename(const char* from, const char* to) {
    if (failure == Rename) return false;
    files[to] = files.at(from);
    files.erase(from);
    ++commits;
    return true;
  }
};
extern FakeFS LittleFS;

class File {
  struct Handle {
    std::string path;
    std::vector<uint8_t> data;
    bool writing;
  };
  std::shared_ptr<Handle> handle;
 public:
  File() {}
  File(const char* path, bool writing) : handle(new Handle{path, LittleFS.files[path], writing}) {
    if (writing) handle->data.clear();
  }
  explicit operator bool() const { return bool(handle); }
  size_t size() const { return handle->data.size(); }
  size_t write(const uint8_t* data, size_t size) {
    if (LittleFS.failure == FakeFS::ShortWrite) --size;
    handle->data.assign(data, data + size);
    return size;
  }
  int read(uint8_t* data, size_t size) {
    if (size > handle->data.size()) size = handle->data.size();
    memcpy(data, handle->data.data(), size);
    return int(size);
  }
  void flush() {
    if (handle->writing && LittleFS.failure != FakeFS::Flush)
      LittleFS.files[handle->path] = handle->data;
  }
  int getWriteError() const { return 0; }
  void close() { flush(); handle.reset(); }
};

inline File FakeFS::open(const char* path, const char* mode) {
  const bool writing = strcmp(mode, "w") == 0;
  if (!mounted || (writing && failure == OpenWrite) || (!writing && !exists(path)) ||
      (!writing && failure == ReadBack && strcmp(path, "/ratgdo.state") == 0))
    return File();
  return File(path, writing);
}
#endif
