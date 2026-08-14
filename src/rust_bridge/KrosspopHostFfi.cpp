#include "KrosspopHostFfi.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <new>

#ifndef SIMULATOR
#include <esp_random.h>
#endif

// Ownership crosses the C ABI to Rust, which releases it via
// krosspop_file_close, so a raw allocation is correct here rather than a
// smart pointer. `new` is not nothrow on ESP32 (AGENTS.md), hence the
// explicit std::nothrow.
struct KrosspopFile {
  FsFile file;
};

KrosspopFile* krosspop_file_open(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }
  auto* handle = new (std::nothrow) KrosspopFile();
  if (handle == nullptr) {
    LOG_ERR("KROSSPOP", "Out of memory opening %s", path);
    return nullptr;
  }
  if (!Storage.openFileForRead("KROSSPOP", path, handle->file)) {
    delete handle;
    return nullptr;
  }
  return handle;
}

void krosspop_file_close(KrosspopFile* file) {
  if (file == nullptr) {
    return;
  }
  file->file.close();
  delete file;
}

uint32_t krosspop_file_size(KrosspopFile* file) {
  return file == nullptr ? 0 : static_cast<uint32_t>(file->file.size());
}

uint32_t krosspop_file_read(KrosspopFile* file, uint8_t* out, const uint32_t len) {
  if (file == nullptr || out == nullptr || len == 0) {
    return 0;
  }
  const int read = file->file.read(out, len);
  return read > 0 ? static_cast<uint32_t>(read) : 0;
}

bool krosspop_file_seek(KrosspopFile* file, const uint32_t offset) {
  return file != nullptr && file->file.seekSet(offset);
}

uint32_t krosspop_random_u32() {
#ifdef SIMULATOR
  // The simulator has no hardware RNG; compose a full 32 bits from Arduino's
  // random(), which is what the rest of the app already uses there.
  return (static_cast<uint32_t>(random(0x10000)) << 16) ^ static_cast<uint32_t>(random(0x10000));
#else
  // Arduino's random() is already backed by esp_random() unless randomSeed()
  // is called (which this firmware never does); call it directly for the full
  // range instead of random()'s modulo-reduced result.
  return esp_random();
#endif
}

void krosspop_log(const char* message) {
  if (message != nullptr) {
    LOG_INF("KROSSPOP", "%s", message);
  }
}
