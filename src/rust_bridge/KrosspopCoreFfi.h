#pragma once

// FFI declarations for rust/krosspop_core. See ROADMAP.md's "Rust
// Integration" section for the split/build details.
//
// krosspop_poc_add is the link-verification POC; remove once real
// DB-engine FFI functions replace it.
extern "C" int32_t krosspop_poc_add(int32_t a, int32_t b);
