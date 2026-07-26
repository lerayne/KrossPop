//! POC: proves a Rust static lib can be linked into the C++ firmware and
//! called across a plain C ABI boundary. See ROADMAP.md's "Rust Integration".
#![no_std]

// Panics here mean a truly-impossible state (this codebase's `assert(false)`
// equivalent) — routine errors should return an error code instead.
// `not(test)`: avoids conflicting with std's panic_impl under cargo test/rust-analyzer.
#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

/// Proves the static lib links and an FFI call round-trips. Not real logic.
#[no_mangle]
pub extern "C" fn krosspop_poc_add(a: i32, b: i32) -> i32 {
    a + b
}
