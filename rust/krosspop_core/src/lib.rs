//! POC: proves a Rust static library can be linked into the existing C++
//! firmware and called across a plain C ABI boundary. No ESP-IDF/std access —
//! see ROADMAP.md's "Rust Integration" section for why that's deliberate.
#![no_std]

use core::panic::PanicInfo;

// A panic here means a truly-impossible state (this project's `assert(false)`
// equivalent), never a routine error path — routine failures should be
// modeled as a returned error code instead. See ROADMAP.md's panic policy.
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

/// Trivial proof-of-concept: confirms the static lib links and a plain C ABI
/// call round-trips correctly. Not part of the real DB engine.
#[no_mangle]
pub extern "C" fn krosspop_poc_add(a: i32, b: i32) -> i32 {
    a + b
}
