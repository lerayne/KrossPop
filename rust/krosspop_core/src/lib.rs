//! POC: proves a Rust static lib can be linked into the C++ firmware and
//! called across a plain C ABI boundary. See ROADMAP.md's "Rust Integration".
#![no_std]

extern crate alloc;

pub mod host;

// Forwards to the same malloc/free the C++ firmware already links against,
// so Rust shares one heap with everything else instead of reserving its own
// arena. `not(test)`: cargo test/rust-analyzer's test build already has
// std's own global allocator; registering a second one would conflict.
#[cfg(not(test))]
use core::alloc::{GlobalAlloc, Layout};

#[cfg(not(test))]
extern "C" {
    fn malloc(size: usize) -> *mut u8;
    fn free(ptr: *mut u8);
}

#[cfg(not(test))]
struct LibcAllocator;

#[cfg(not(test))]
unsafe impl GlobalAlloc for LibcAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        malloc(layout.size())
    }
    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        free(ptr)
    }
}

#[cfg(not(test))]
#[global_allocator]
static ALLOCATOR: LibcAllocator = LibcAllocator;

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
