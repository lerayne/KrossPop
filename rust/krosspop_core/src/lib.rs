//! POC: proves a Rust static library can be linked into the existing C++
//! firmware and called across a plain C ABI boundary. No ESP-IDF/std access —
//! see ROADMAP.md's "Rust Integration" section for why that's deliberate.
#![no_std]

// A panic here means a truly-impossible state (this project's `assert(false)`
// equivalent), never a routine error path — routine failures should be
// modeled as a returned error code instead. See ROADMAP.md's panic policy.
//
// Gated on `not(test)`: `cargo test`/rust-analyzer's test analysis links
// Rust's std test harness, which brings in std's own `panic_impl` — defining
// ours unconditionally would conflict with it (E0152, duplicate lang item).
// Real firmware builds (`cargo build --release --target ...`, no test
// harness involved) are unaffected either way.
#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

/// Trivial proof-of-concept: confirms the static lib links and a plain C ABI
/// call round-trips correctly. Not part of the real DB engine.
#[no_mangle]
pub extern "C" fn krosspop_poc_add(a: i32, b: i32) -> i32 {
    a + b
}
