//! Safe wrappers over the C++ host functions declared in
//! `src/rust_bridge/KrosspopHostFfi.h`.
//!
//! All `unsafe` in this crate should live here. Everything above this module
//! works with `File`, slices and plain values, never raw pointers.

use core::ffi::{c_char, CStr};
use core::marker::{PhantomData, PhantomPinned};

/// The C++ `KrosspopFile`, opaque on this side.
///
/// Zero-sized and impossible to construct from Rust, so the only way to get
/// one is from `krosspop_file_open`. The `PhantomData` keeps it `!Send`,
/// `!Sync` and `!Unpin`, matching a handle we know nothing about.
#[repr(C)]
pub struct KrosspopFile {
    _data: [u8; 0],
    _marker: PhantomData<(*mut u8, PhantomPinned)>,
}

extern "C" {
    fn krosspop_file_open(path: *const c_char) -> *mut KrosspopFile;
    fn krosspop_file_close(file: *mut KrosspopFile);
    fn krosspop_file_size(file: *mut KrosspopFile) -> u32;
    fn krosspop_file_read(file: *mut KrosspopFile, out: *mut u8, len: u32) -> u32;
    fn krosspop_file_seek(file: *mut KrosspopFile, offset: u32) -> bool;
    fn krosspop_random_u32() -> u32;
    fn krosspop_log(message: *const c_char);
}

/// An open file on the SD card.
///
/// Closing is automatic: `Drop` releases the C++ handle when the value goes
/// out of scope, including on an early `return` or `?`. There is no way to
/// leak one short of `core::mem::forget`.
pub struct File {
    handle: *mut KrosspopFile,
}

impl File {
    /// Opens `path` for reading. `None` if it does not exist or cannot be read.
    ///
    /// Paths are usually literals: `File::open(c"/krosspop/cards.data.bin")`.
    pub fn open(path: &CStr) -> Option<Self> {
        // SAFETY: `path.as_ptr()` is a valid null-terminated C string that
        // outlives this call, and the C++ side only reads it.
        let handle = unsafe { krosspop_file_open(path.as_ptr()) };
        if handle.is_null() {
            None
        } else {
            Some(Self { handle })
        }
    }

    /// Total size in bytes, or 0 if unavailable.
    pub fn size(&self) -> u32 {
        // SAFETY: `handle` is non-null for the lifetime of `self` — `open`
        // rejects null, and only `Drop` invalidates it.
        unsafe { krosspop_file_size(self.handle) }
    }

    /// Moves the read cursor to an absolute offset. `false` on failure.
    pub fn seek(&mut self, offset: u32) -> bool {
        // SAFETY: as above; `handle` is valid and non-null.
        unsafe { krosspop_file_seek(self.handle, offset) }
    }

    /// Reads into `out`, returning how many bytes were actually read. May be
    /// fewer than requested; use [`read_exact`](Self::read_exact) when you
    /// need the whole buffer filled.
    pub fn read(&mut self, out: &mut [u8]) -> usize {
        // The host API takes a u32 length. usize is 32-bit on the device but
        // 64-bit in the simulator build, so clamp rather than truncate.
        let len = out.len().min(u32::MAX as usize) as u32;
        // SAFETY: `out` is valid for `len` writes by construction, and the
        // C++ side writes at most `len` bytes and never retains the pointer.
        let read = unsafe { krosspop_file_read(self.handle, out.as_mut_ptr(), len) };
        (read as usize).min(out.len())
    }

    /// Fills `out` completely, looping because one `read` may return less.
    /// `false` if the file ended early.
    pub fn read_exact(&mut self, out: &mut [u8]) -> bool {
        let mut filled = 0;
        while filled < out.len() {
            let read = self.read(&mut out[filled..]);
            if read == 0 {
                return false;
            }
            filled += read;
        }
        true
    }

    /// Seek then fill — the record-lookup primitive the database engine uses.
    pub fn read_exact_at(&mut self, offset: u32, out: &mut [u8]) -> bool {
        self.seek(offset) && self.read_exact(out)
    }
}

impl Drop for File {
    fn drop(&mut self) {
        // SAFETY: `handle` is non-null and has not been closed — `Drop` runs
        // exactly once, and nothing else calls the close function.
        unsafe { krosspop_file_close(self.handle) };
    }
}

/// Full-range entropy from the device's hardware RNG.
pub fn random_u32() -> u32 {
    // SAFETY: no arguments, no pointers, always safe to call.
    unsafe { krosspop_random_u32() }
}

/// Logs a message through the firmware's logger.
pub fn log(message: &CStr) {
    // SAFETY: valid null-terminated string that outlives the call; the C++
    // side only reads it.
    unsafe { krosspop_log(message.as_ptr()) };
}

/// Backs the [`klog!`](crate::klog) macro. Allocates, so it silently does
/// nothing if the message contains an interior nul byte.
pub fn log_fmt(args: core::fmt::Arguments) {
    use alloc::string::ToString;
    if let Ok(message) = alloc::ffi::CString::new(args.to_string()) {
        log(&message);
    }
}

/// Formatted logging: `klog!("picked card {} tier {}", id, tier);`
#[macro_export]
macro_rules! klog {
    ($($arg:tt)*) => {
        $crate::host::log_fmt(format_args!($($arg)*))
    };
}
