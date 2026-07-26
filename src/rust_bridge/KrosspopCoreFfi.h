#pragma once

// FFI declarations for rust/krosspop_core (a no_std, alloc-free Rust static
// lib — see ROADMAP.md's "Rust Integration" section). Only linked into real
// firmware builds (see [base] in platformio.ini) — never the simulator,
// which builds natively for the host and cannot link a riscv32imc static
// library.
//
// krosspop_poc_add is the original link-verification POC (confirmed working
// on X3 hardware); remove it once real DB-engine FFI functions replace it.
extern "C" int32_t krosspop_poc_add(int32_t a, int32_t b);
