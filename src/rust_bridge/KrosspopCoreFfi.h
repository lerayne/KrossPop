#pragma once

// FFI declarations for rust/krosspop_core (a no_std, alloc-free Rust static
// lib — see ROADMAP.md's "Rust Integration" section). The crate makes no
// ESP-IDF/std calls, so it's portable: scripts/build_rust.py compiles it for
// the ESP32-C3's RISC-V target in real firmware envs, and for the host's
// native target in the simulator env — so these declarations, and any code
// calling them, need no `#ifndef SIMULATOR` guarding.
//
// krosspop_poc_add is the original link-verification POC (confirmed working
// on X3 hardware and in the simulator); remove it once real DB-engine FFI
// functions replace it.
extern "C" int32_t krosspop_poc_add(int32_t a, int32_t b);
