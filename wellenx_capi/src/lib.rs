// wellenx_capi — Minimal C FFI extension over the Wellen waveform library
// BSD-3-Clause License
//
// xdebug-fst extension crate: exposes only the queries missing from
// wellen_capi — ASCII bit-string value access (2/4/9-state) and
// change-time index iteration.

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use wellen::simple::Waveform;
use wellen::*;

// ── Opaque handle ──

pub struct WellenxDb {
    wave: Waveform,
}

// ── Helpers ──

fn sig_from_u32(r: u32) -> Option<SignalRef> {
    SignalRef::from_index(r as usize)
}

// ── FFI: Lifecycle ──

/// Open a waveform file (FST/VCD/GHW auto-detected).
/// Returns null on failure.
#[no_mangle]
pub extern "C" fn wellenx_open(path: *const c_char) -> *mut WellenxDb {
    if path.is_null() {
        return std::ptr::null_mut();
    }
    let cstr = unsafe { CStr::from_ptr(path) };
    let path_str = match cstr.to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return std::ptr::null_mut(),
    };
    match wellen::simple::read(&path_str) {
        Ok(wave) => Box::into_raw(Box::new(WellenxDb { wave })),
        Err(_) => std::ptr::null_mut(),
    }
}

/// Close and free all resources.
#[no_mangle]
pub extern "C" fn wellenx_close(db: *mut WellenxDb) {
    if !db.is_null() {
        unsafe { drop(Box::from_raw(db)) };
    }
}

// ── Signal loading ──

/// Load signals by ref. Returns number loaded.
#[no_mangle]
pub extern "C" fn wellenx_load_signals(db: *mut WellenxDb, refs: *const u32, count: u32) -> i32 {
    if db.is_null() || refs.is_null() || count == 0 {
        return 0;
    }
    let db = unsafe { &mut *db };
    let refs_slice = unsafe { std::slice::from_raw_parts(refs, count as usize) };
    let signal_refs: Vec<SignalRef> = refs_slice
        .iter()
        .filter_map(|&r| sig_from_u32(r))
        .collect();
    db.wave.load_signals(&signal_refs);
    signal_refs.len() as i32
}

/// Unload signals to free memory.
#[no_mangle]
pub extern "C" fn wellenx_unload_signals(db: *mut WellenxDb, refs: *const u32, count: u32) {
    if db.is_null() || refs.is_null() || count == 0 {
        return;
    }
    let db = unsafe { &mut *db };
    let refs_slice = unsafe { std::slice::from_raw_parts(refs, count as usize) };
    let signal_refs: Vec<SignalRef> = refs_slice
        .iter()
        .filter_map(|&r| sig_from_u32(r))
        .collect();
    db.wave.unload_signals(&signal_refs);
}

// ── Core queries ──

/// Read signal value as ASCII bit string ('0','1','x','z','h','u','w','l','-').
/// out_str must have capacity >= signal width. Returns 0 on success, -1 on
/// error, -2 for string signals (out_len = string length), -3 for real
/// signals (out_len = 8, little-endian f64 bytes).
#[no_mangle]
pub extern "C" fn wellenx_signal_value_bits_at_offset(
    db: *const WellenxDb,
    signal_ref: u32,
    start: u32,
    element: u16,
    out_str: *mut c_char,
    out_len: *mut u32,
) -> i32 {
    if db.is_null() || out_str.is_null() || out_len.is_null() {
        return -1;
    }
    let db = unsafe { &*db };
    let sr = match sig_from_u32(signal_ref) {
        Some(s) => s,
        None => return -1,
    };
    let signal = match db.wave.get_signal(sr) {
        Some(s) => s,
        None => return -1,
    };
    let value = signal.data().get_value_at(start as usize + element as usize);

    match value {
        SignalValueRef::BitVec(bv) => {
            let s = bv.bit_string();
            let bytes = s.as_bytes();
            unsafe {
                std::ptr::copy_nonoverlapping(bytes.as_ptr(), out_str as *mut u8, bytes.len());
                *out_len = bytes.len() as u32;
            }
            0
        }
        SignalValueRef::String(s) => {
            let bytes = s.as_bytes();
            unsafe {
                std::ptr::copy_nonoverlapping(bytes.as_ptr(), out_str as *mut u8, bytes.len());
                *out_len = bytes.len() as u32;
            }
            -2
        }
        SignalValueRef::Real(r) => {
            unsafe {
                std::ptr::copy_nonoverlapping(r.to_le_bytes().as_ptr(), out_str as *mut u8, 8);
                *out_len = 8;
            }
            -3
        }
        SignalValueRef::Event => {
            unsafe { *out_len = 0 };
            0
        }
    }
}

/// Copy the time indices at which a signal changes into out[].
/// out must have capacity >= num_changes. Returns count copied, -1 on error.
#[no_mangle]
pub extern "C" fn wellenx_signal_time_indices(
    db: *const WellenxDb,
    signal_ref: u32,
    out: *mut u32,
) -> i32 {
    if db.is_null() || out.is_null() {
        return -1;
    }
    let db = unsafe { &*db };
    let sr = match sig_from_u32(signal_ref) {
        Some(s) => s,
        None => return -1,
    };
    let signal = match db.wave.get_signal(sr) {
        Some(s) => s,
        None => return -1,
    };
    let indices = signal.time_indices();
    unsafe {
        std::ptr::copy_nonoverlapping(indices.as_ptr(), out, indices.len());
    }
    indices.len() as i32
}
