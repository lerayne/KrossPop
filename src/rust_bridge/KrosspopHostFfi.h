#pragma once

#include <cstdint>

// Platform services that rust/krosspop_core calls back into — the reverse
// direction of KrosspopCoreFfi.h.
//
// Deliberately primitive-only: file bytes, entropy, logging. No card-database
// knowledge lives here. Record layout, offsets, tier weighting and draw logic
// all stay in Rust, so this set should grow logarithmically (new features
// reuse these) rather than once per feature. See ROADMAP.md, "Rust Integration".

// Opaque to Rust: it holds the pointer without knowing what's inside.
struct KrosspopFile;

extern "C" {

// Opens a file for reading. Returns nullptr on failure. Ownership passes to
// the caller, which must release it with krosspop_file_close.
KrosspopFile* krosspop_file_open(const char* path);

void krosspop_file_close(KrosspopFile* file);

// Total size in bytes, or 0 if unavailable.
uint32_t krosspop_file_size(KrosspopFile* file);

// Reads up to `len` bytes from the current position into `out`.
// Returns how many bytes were actually read (0 on failure or EOF).
uint32_t krosspop_file_read(KrosspopFile* file, uint8_t* out, uint32_t len);

// Absolute seek from the start of the file. Returns false on failure.
bool krosspop_file_seek(KrosspopFile* file, uint32_t offset);

// Full-range entropy. Hardware RNG on device.
uint32_t krosspop_random_u32(void);

// Emits a null-terminated message through the firmware's logger.
void krosspop_log(const char* message);
}
