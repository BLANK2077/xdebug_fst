// Copyright 2024-2026 Cornell University
// released under BSD 3-Clause License
//
// C FFI bindings for the Wellen waveform library (FST/VCD/GHW).

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use wellen::simple::Waveform;
use wellen::*;

thread_local! {
    static LAST_OPEN_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

// ── Opaque handle ──

pub struct WellenDb {
    wave: Waveform,
    /// Cached full names for fast C string return
    var_names: HashMap<usize, CString>,
    scope_names: HashMap<usize, CString>,
    /// Cached signal info
    signal_infos: Vec<Option<CachedSignalInfo>>,
}

#[derive(Clone)]
struct CachedSignalInfo {
    encoding: WellenSignalEncoding,
    num_changes: u32,
    max_states: u32,
    width: u32,
    bytes_per_entry: u32,
    has_meta_byte: i32,
}

// ── C-compatible enums ──

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WellenSignalEncoding {
    BitVector = 0,
    Real = 1,
    String = 2,
    Event = 3,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WellenScopeKind {
    Other = 0,
    Module = 1,
    Interface = 2,
}

#[repr(C)]
pub struct WellenSignalInfo {
    pub signal_ref: u32,
    pub encoding: WellenSignalEncoding,
    pub num_changes: u32,
    pub max_states: u32,
    pub width: u32,
    pub bytes_per_entry: u32,
    pub has_meta_byte: i32,
}

// ── Helpers ──

fn encoding_to_c(enc: SignalEncoding) -> (WellenSignalEncoding, u32) {
    match enc {
        SignalEncoding::BitVector(0) => (WellenSignalEncoding::Event, 0),
        SignalEncoding::BitVector(w) => (WellenSignalEncoding::BitVector, w),
        SignalEncoding::Real => (WellenSignalEncoding::Real, 64),
        SignalEncoding::String => (WellenSignalEncoding::String, 0),
    }
}

fn scope_from_u32(r: u32) -> Option<ScopeRef> {
    if r == 0 {
        None
    } else {
        ScopeRef::from_index((r - 1) as usize)
    }
}

fn var_from_u32(r: u32) -> Option<VarRef> {
    if r == 0 {
        None
    } else {
        VarRef::from_index((r - 1) as usize)
    }
}

fn sig_from_u32(r: u32) -> Option<SignalRef> {
    if r == 0 {
        None
    } else {
        SignalRef::from_index((r - 1) as usize)
    }
}

// ── FFI: Lifecycle ──

/// Open a waveform file (FST/VCD/GHW auto-detected).
/// Returns null on failure; call `wellen_last_open_error()` for details.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_open(path: *const c_char) -> *mut WellenDb {
    // Clear previous error
    LAST_OPEN_ERROR.with(|e| *e.borrow_mut() = None);

    if path.is_null() {
        LAST_OPEN_ERROR.with(|e| {
            *e.borrow_mut() = Some(CString::new("null path").unwrap_or_default());
        });
        return std::ptr::null_mut();
    }
    let path_str = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(s) => s,
        Err(e) => {
            LAST_OPEN_ERROR.with(|err| {
                *err.borrow_mut() =
                    Some(CString::new(format!("invalid UTF-8 path: {e}")).unwrap_or_default());
            });
            return std::ptr::null_mut();
        }
    };

    match wellen::simple::read(path_str) {
        Ok(wave) => {
            let signal_count = wave.hierarchy().signals().count();
            let mut signal_infos = Vec::with_capacity(signal_count);
            signal_infos.resize_with(signal_count, || None);

            let db = Box::new(WellenDb {
                wave,
                var_names: HashMap::new(),
                scope_names: HashMap::new(),
                signal_infos,
            });
            Box::into_raw(db)
        }
        Err(e) => {
            LAST_OPEN_ERROR.with(|err| {
                *err.borrow_mut() = Some(CString::new(e.to_string()).unwrap_or_default());
            });
            std::ptr::null_mut()
        }
    }
}

/// If wellen_open returned NULL, returns the error message.
/// Otherwise returns NULL.
/// The returned pointer is valid until the next wellen_open call on this thread.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_open_error(_db: *const WellenDb) -> *const c_char {
    LAST_OPEN_ERROR.with(|e| {
        e.borrow()
            .as_ref()
            .map(|s| s.as_ptr())
            .unwrap_or(std::ptr::null())
    })
}

/// Close and free all resources.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_close(db: *mut WellenDb) {
    if db.is_null() {
        return;
    }
    unsafe {
        // Drop as WellenDb. If it was actually an OpenError, that's also fine
        // since both are Box-allocated and Drop will clean up.
        drop(Box::from_raw(db));
    }
}

// ── FFI: Time table ──

#[unsafe(no_mangle)]
pub extern "C" fn wellen_time_count(db: *const WellenDb) -> u32 {
    if db.is_null() {
        return 0;
    }
    unsafe { &*db }.wave.time_table().len() as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_get_times(
    db: *const WellenDb,
    out: *mut u64,
    offset: u32,
    count: u32,
) -> i32 {
    if db.is_null() || out.is_null() {
        return -1;
    }
    let db = unsafe { &*db };
    let tt = db.wave.time_table();
    let start = offset as usize;
    if start >= tt.len() {
        return 0;
    }
    let end = (start + count as usize).min(tt.len());
    let n = end - start;
    let out_slice = unsafe { std::slice::from_raw_parts_mut(out, n) };
    out_slice.copy_from_slice(&tt[start..end]);
    n as i32
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_timescale(
    db: *const WellenDb,
    out_factor: *mut u32,
    out_exponent: *mut i32,
) -> i32 {
    if db.is_null() || out_factor.is_null() || out_exponent.is_null() {
        return -1;
    }
    let hierarchy = unsafe { &*db }.wave.hierarchy();
    let timescale = match hierarchy.timescale() {
        Some(value) => value,
        None => return -1,
    };
    let exponent = match timescale.unit.to_exponent() {
        Some(value) => value,
        None => return -1,
    };
    unsafe {
        *out_factor = timescale.factor;
        *out_exponent = exponent as i32;
    }
    0
}

// ── FFI: Hierarchy traversal ──

#[unsafe(no_mangle)]
pub extern "C" fn wellen_root_scope_count(db: *const WellenDb) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    h.scopes().count() as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_root_scope_at(db: *const WellenDb, idx: u32) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    h.scopes()
        .nth(idx as usize)
        .map(|s| s.index() as u32 + 1)
        .unwrap_or(0)
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_count(db: *const WellenDb) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    db.wave.hierarchy().all_scopes().count() as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_at(db: *const WellenDb, idx: u32) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    h.all_scopes()
        .nth(idx as usize)
        .map(|s| s.index() as u32 + 1)
        .unwrap_or(0)
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_child_count(db: *const WellenDb, scope_ref: u32) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    let scope = match scope_from_u32(scope_ref) {
        Some(s) => s,
        None => return 0,
    };
    h[scope].scopes(h).count() as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_child_at(db: *const WellenDb, scope_ref: u32, idx: u32) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    let scope = match scope_from_u32(scope_ref) {
        Some(s) => s,
        None => return 0,
    };
    h[scope]
        .scopes(h)
        .nth(idx as usize)
        .map(|s| s.index() as u32 + 1)
        .unwrap_or(0)
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_var_count(db: *const WellenDb, scope_ref: u32) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    let scope = match scope_from_u32(scope_ref) {
        Some(s) => s,
        None => return 0,
    };
    h[scope].vars(h).count() as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_var_at(db: *const WellenDb, scope_ref: u32, idx: u32) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    let scope = match scope_from_u32(scope_ref) {
        Some(s) => s,
        None => return 0,
    };
    h[scope]
        .vars(h)
        .nth(idx as usize)
        .map(|v| v.index() as u32 + 1)
        .unwrap_or(0)
}

/// Get scope name. Valid until wellen_close.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_name(db: *mut WellenDb, scope_ref: u32) -> *const c_char {
    if db.is_null() {
        return std::ptr::null();
    }
    let db = unsafe { &mut *db };
    let h = db.wave.hierarchy();
    let scope = match scope_from_u32(scope_ref) {
        Some(s) => s,
        None => return std::ptr::null(),
    };
    let name = h[scope].name(h).to_string();
    db.scope_names
        .entry(scope.index())
        .or_insert_with(|| CString::new(name).unwrap_or_default())
        .as_ptr()
}

/// Get full hierarchical scope name. Valid until wellen_close.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_full_name(db: *mut WellenDb, scope_ref: u32) -> *const c_char {
    if db.is_null() {
        return std::ptr::null();
    }
    let db = unsafe { &mut *db };
    let h = db.wave.hierarchy();
    let scope = match scope_from_u32(scope_ref) {
        Some(s) => s,
        None => return std::ptr::null(),
    };
    let name = h[scope].full_name(h);
    db.scope_names
        .entry(scope.index() | 0x8000_0000)
        .or_insert_with(|| CString::new(name).unwrap_or_default())
        .as_ptr()
}

/// Get the component/module definition name. Valid until wellen_close.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_component(
    db: *mut WellenDb,
    scope_ref: u32,
) -> *const c_char {
    if db.is_null() {
        return std::ptr::null();
    }
    let db = unsafe { &mut *db };
    let hierarchy = db.wave.hierarchy();
    let scope = match scope_from_u32(scope_ref) {
        Some(scope) => scope,
        None => return std::ptr::null(),
    };
    let component = match hierarchy[scope].component(hierarchy) {
        Some(component) if !component.is_empty() => component.to_string(),
        _ => return std::ptr::null(),
    };
    db.scope_names
        .entry(scope.index() | 0x4000_0000)
        .or_insert_with(|| CString::new(component).unwrap_or_default())
        .as_ptr()
}

/// Get the public scope category used by the C++ hierarchy projection.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_scope_kind(
    db: *const WellenDb,
    scope_ref: u32,
) -> WellenScopeKind {
    if db.is_null() {
        return WellenScopeKind::Other;
    }
    let db = unsafe { &*db };
    let hierarchy = db.wave.hierarchy();
    let scope = match scope_from_u32(scope_ref) {
        Some(scope) => scope,
        None => return WellenScopeKind::Other,
    };
    match hierarchy[scope].scope_type() {
        ScopeType::Module => WellenScopeKind::Module,
        ScopeType::Interface => WellenScopeKind::Interface,
        _ => WellenScopeKind::Other,
    }
}

/// Get var local name. Valid until wellen_close.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_var_name(db: *mut WellenDb, var_ref: u32) -> *const c_char {
    if db.is_null() {
        return std::ptr::null();
    }
    let db = unsafe { &mut *db };
    let h = db.wave.hierarchy();
    let var = match var_from_u32(var_ref) {
        Some(v) => v,
        None => return std::ptr::null(),
    };
    let name = h[var].name(h).to_string();
    db.var_names
        .entry(var.index())
        .or_insert_with(|| CString::new(name).unwrap_or_default())
        .as_ptr()
}

/// Get var full hierarchical name. Valid until wellen_close.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_var_full_name(db: *mut WellenDb, var_ref: u32) -> *const c_char {
    if db.is_null() {
        return std::ptr::null();
    }
    let db = unsafe { &mut *db };
    let h = db.wave.hierarchy();
    let var = match var_from_u32(var_ref) {
        Some(v) => v,
        None => return std::ptr::null(),
    };
    let name = h[var].full_name(h).to_string();
    db.var_names
        .entry(var.index() | 0x8000_0000) // use high bit to distinguish from local name cache
        .or_insert_with(|| CString::new(name).unwrap_or_default())
        .as_ptr()
}

/// Get the 1-based C signal reference for a variable.
///
/// Wellen's native `SignalRef(0)` is valid, so the C ABI reserves zero for an
/// invalid reference and exposes every native signal index plus one.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_var_signal_ref(db: *const WellenDb, var_ref: u32) -> u32 {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    let var = match var_from_u32(var_ref) {
        Some(v) => v,
        None => return 0,
    };
    let sr = h[var].signal_ref();
    sr.index() as u32 + 1
}

/// Get signal encoding and width.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_var_encoding(
    db: *const WellenDb,
    var_ref: u32,
    out_width: *mut u32,
) -> WellenSignalEncoding {
    if db.is_null() || var_ref == 0 || out_width.is_null() {
        unsafe {
            if !out_width.is_null() {
                *out_width = 0;
            }
        }
        return WellenSignalEncoding::BitVector;
    }
    let db = unsafe { &*db };
    let h = db.wave.hierarchy();
    let var = match var_from_u32(var_ref) {
        Some(v) => v,
        None => {
            unsafe { *out_width = 0 };
            return WellenSignalEncoding::BitVector;
        }
    };
    let (enc, w) = encoding_to_c(h[var].signal_encoding(h));
    unsafe { *out_width = w };
    enc
}

/// Get the declared packed index range for a variable.
#[unsafe(no_mangle)]
pub extern "C" fn wellen_var_index(
    db: *const WellenDb,
    var_ref: u32,
    out_msb: *mut i64,
    out_lsb: *mut i64,
) -> i32 {
    if db.is_null() || out_msb.is_null() || out_lsb.is_null() {
        return -1;
    }
    let db = unsafe { &*db };
    let hierarchy = db.wave.hierarchy();
    let var = match var_from_u32(var_ref) {
        Some(var) => var,
        None => return -1,
    };
    let index = match hierarchy[var].index() {
        Some(index) => index,
        None => return -1,
    };
    unsafe {
        *out_msb = index.msb();
        *out_lsb = index.lsb();
    }
    0
}

// ── FFI: Signal loading ──

#[unsafe(no_mangle)]
pub extern "C" fn wellen_load_signals(db: *mut WellenDb, refs: *const u32, count: u32) -> i32 {
    if db.is_null() || refs.is_null() || count == 0 {
        return 0;
    }
    let db = unsafe { &mut *db };
    let refs_slice = unsafe { std::slice::from_raw_parts(refs, count as usize) };
    let signal_refs: Vec<SignalRef> = refs_slice.iter().filter_map(|&r| sig_from_u32(r)).collect();

    db.wave.load_signals(&signal_refs);

    for &sr in &signal_refs {
        if let Some(signal) = db.wave.get_signal(sr) {
            let max_states = signal.max_states().map(|s| s as u32).unwrap_or(2u32);
            let num_changes = signal.iter_changes().count() as u32;
            let mut encoding = WellenSignalEncoding::BitVector;
            let mut width = 0u32;
            for scope in db.wave.hierarchy().all_scopes() {
                for var in db.wave.hierarchy()[scope].vars(db.wave.hierarchy()) {
                    if db.wave.hierarchy()[var].signal_ref() == sr {
                        (encoding, width) = encoding_to_c(
                            db.wave.hierarchy()[var].signal_encoding(db.wave.hierarchy()),
                        );
                        break;
                    }
                }
            }
            let bytes_per_entry = match encoding {
                WellenSignalEncoding::BitVector => signal
                    .max_states()
                    .map(|states| states.bytes_required(width) as u32)
                    .unwrap_or_else(|| width.div_ceil(8)),
                WellenSignalEncoding::Real => 8,
                WellenSignalEncoding::String | WellenSignalEncoding::Event => 0,
            };

            let idx = sr.index();
            if idx >= db.signal_infos.len() {
                db.signal_infos.resize(idx + 1, None);
            }
            db.signal_infos[idx] = Some(CachedSignalInfo {
                encoding,
                num_changes,
                max_states,
                width,
                bytes_per_entry,
                has_meta_byte: if max_states > 2 && width >= 8 { 1 } else { 0 },
            });
        }
    }

    signal_refs.len() as i32
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_unload_signals(db: *mut WellenDb, refs: *const u32, count: u32) {
    if db.is_null() || refs.is_null() || count == 0 {
        return;
    }
    let db = unsafe { &mut *db };
    let refs_slice = unsafe { std::slice::from_raw_parts(refs, count as usize) };
    let signal_refs: Vec<SignalRef> = refs_slice.iter().filter_map(|&r| sig_from_u32(r)).collect();
    db.wave.unload_signals(&signal_refs);
    for sr in &signal_refs {
        let idx = sr.index();
        if idx < db.signal_infos.len() {
            db.signal_infos[idx] = None;
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_signal_info(
    db: *const WellenDb,
    signal_ref: u32,
    info: *mut WellenSignalInfo,
) -> i32 {
    if db.is_null() || info.is_null() {
        return -1;
    }
    let db = unsafe { &*db };
    let idx = match signal_ref.checked_sub(1) {
        Some(idx) => idx as usize,
        None => return -1,
    };
    if idx >= db.signal_infos.len() {
        return -1;
    }
    match &db.signal_infos[idx] {
        Some(c) => unsafe {
            (*info).signal_ref = signal_ref;
            (*info).encoding = c.encoding;
            (*info).num_changes = c.num_changes;
            (*info).max_states = c.max_states;
            (*info).width = c.width;
            (*info).bytes_per_entry = c.bytes_per_entry;
            (*info).has_meta_byte = c.has_meta_byte;
            0
        },
        None => -1,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_signal_typed_value_at_offset(
    db: *const WellenDb,
    signal_ref: u32,
    start: u32,
    element: u16,
    out_text: *mut c_char,
    text_capacity: u32,
    out_text_len: *mut u32,
    out_real: *mut f64,
    out_encoding: *mut WellenSignalEncoding,
) -> i32 {
    if db.is_null() || out_text_len.is_null() || out_real.is_null() || out_encoding.is_null() {
        return -1;
    }
    let db = unsafe { &*db };
    let signal = match sig_from_u32(signal_ref).and_then(|sr| db.wave.get_signal(sr)) {
        Some(value) => value,
        None => return -1,
    };
    let value = signal
        .data()
        .get_value_at(start as usize + element as usize);
    let (encoding, text): (WellenSignalEncoding, Option<&[u8]>) = match value {
        SignalValueRef::BitVec(bits) => {
            let rendered = bits.bit_string();
            unsafe {
                *out_encoding = WellenSignalEncoding::BitVector;
                *out_text_len = rendered.len() as u32;
                *out_real = 0.0;
            }
            if text_capacity < rendered.len() as u32 {
                return -2;
            }
            if !rendered.is_empty() {
                if out_text.is_null() {
                    return -1;
                }
                unsafe {
                    std::ptr::copy_nonoverlapping(
                        rendered.as_ptr(),
                        out_text as *mut u8,
                        rendered.len(),
                    );
                }
            }
            return 0;
        }
        SignalValueRef::String(string) => (WellenSignalEncoding::String, Some(string.as_bytes())),
        SignalValueRef::Real(real) => {
            unsafe {
                *out_encoding = WellenSignalEncoding::Real;
                *out_text_len = 0;
                *out_real = real;
            }
            return 0;
        }
        SignalValueRef::Event => (WellenSignalEncoding::Event, None),
    };
    let bytes = text.unwrap_or_default();
    unsafe {
        *out_encoding = encoding;
        *out_text_len = bytes.len() as u32;
        *out_real = 0.0;
    }
    if text_capacity < bytes.len() as u32 {
        return -2;
    }
    if !bytes.is_empty() {
        if out_text.is_null() {
            return -1;
        }
        unsafe {
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), out_text as *mut u8, bytes.len());
        }
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn fixture(name: &str) -> CString {
        let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../wellen/inputs")
            .join(name);
        CString::new(path.to_string_lossy().as_bytes()).expect("fixture path contains no NUL")
    }

    fn first_signal_with_encoding(db: *const WellenDb, expected: WellenSignalEncoding) -> u32 {
        let hierarchy = unsafe { &*db }.wave.hierarchy();
        hierarchy
            .all_vars()
            .find_map(|var| {
                let (encoding, _) = encoding_to_c(hierarchy[var].signal_encoding(hierarchy));
                (encoding == expected).then(|| hierarchy[var].signal_ref().index() as u32 + 1)
            })
            .expect("fixture must contain the requested encoding")
    }

    fn first_offset(db: *const WellenDb, signal: u32) -> (u32, u16) {
        assert_eq!(wellen_load_signals(db as *mut WellenDb, &signal, 1), 1);
        let mut start = 0;
        let mut elements = 0;
        let mut time_match = 0;
        let mut next = 0;
        let mut has_next = 0;
        assert_eq!(
            wellen_signal_offset_at(
                db,
                signal,
                0,
                &mut start,
                &mut elements,
                &mut time_match,
                &mut next,
                &mut has_next,
            ),
            0
        );
        (start, elements)
    }

    fn read_typed(
        db: *const WellenDb,
        signal: u32,
        start: u32,
        element: u16,
    ) -> (WellenSignalEncoding, Vec<u8>, f64) {
        let mut length = 0;
        let mut real = 0.0;
        let mut encoding = WellenSignalEncoding::BitVector;
        let probe = wellen_signal_typed_value_at_offset(
            db,
            signal,
            start,
            element,
            std::ptr::null_mut(),
            0,
            &mut length,
            &mut real,
            &mut encoding,
        );
        assert!(probe == 0 || probe == -2);
        let mut text = vec![0_u8; length as usize];
        assert_eq!(
            wellen_signal_typed_value_at_offset(
                db,
                signal,
                start,
                element,
                text.as_mut_ptr() as *mut c_char,
                length,
                &mut length,
                &mut real,
                &mut encoding,
            ),
            0
        );
        (encoding, text, real)
    }

    #[test]
    fn reports_open_errors_without_creating_a_handle() {
        assert!(wellen_open(std::ptr::null()).is_null());
        let error = wellen_open_error(std::ptr::null());
        assert!(!error.is_null());
        assert_eq!(unsafe { CStr::from_ptr(error) }.to_bytes(), b"null path");
        wellen_close(std::ptr::null_mut());
    }

    #[test]
    fn exposes_native_signal_zero_as_c_reference_one() {
        let path = fixture("scope_with_comment.vcd.fst");
        let db = wellen_open(path.as_ptr());
        assert!(!db.is_null(), "fixture must open through the C ABI");
        assert!(wellen_open_error(db).is_null());
        assert!(wellen_time_count(db) > 0);
        let mut factor = 0;
        let mut exponent = 0;
        assert_eq!(wellen_timescale(db, &mut factor, &mut exponent), 0);
        assert_eq!((factor, exponent), (1, -9));

        let mut signals = Vec::new();
        for scope_idx in 0..wellen_scope_count(db) {
            let scope = wellen_scope_at(db, scope_idx);
            for var_idx in 0..wellen_scope_var_count(db, scope) {
                let var = wellen_scope_var_at(db, scope, var_idx);
                let signal = wellen_var_signal_ref(db, var);
                if signal != 0 {
                    signals.push(signal);
                }
            }
        }

        signals.sort_unstable();
        signals.dedup();
        assert_eq!(
            signals.first(),
            Some(&1),
            "native SignalRef(0) must remain addressable"
        );
        let first_signal = signals[0];
        assert_eq!(wellen_load_signals(db, &first_signal, 1), 1);

        let mut info = WellenSignalInfo {
            signal_ref: 0,
            encoding: WellenSignalEncoding::BitVector,
            num_changes: 0,
            max_states: 0,
            width: 0,
            bytes_per_entry: 0,
            has_meta_byte: 0,
        };
        assert_eq!(wellen_signal_info(db, first_signal, &mut info), 0);
        assert_eq!(info.signal_ref, 1);
        assert!(info.num_changes > 0);

        wellen_unload_signals(db, &first_signal, 1);
        assert_eq!(wellen_signal_info(db, first_signal, &mut info), -1);
        wellen_close(db);
    }

    #[test]
    fn exposes_declared_packed_variable_ranges() {
        let path = fixture("verilator/many_sv_datatypes.fst");
        let db = wellen_open(path.as_ptr());
        assert!(!db.is_null(), "fixture must open through the C ABI");
        let hierarchy = unsafe { &*db }.wave.hierarchy();
        let (var, expected) = hierarchy
            .all_vars()
            .find_map(|var| hierarchy[var].index().map(|index| (var, index)))
            .expect("fixture must contain a variable with a declared range");
        let var_ref = var.index() as u32 + 1;
        let mut msb = 0;
        let mut lsb = 0;
        assert_eq!(wellen_var_index(db, var_ref, &mut msb, &mut lsb), 0);
        assert_eq!((msb, lsb), (expected.msb(), expected.lsb()));
        assert_eq!(
            wellen_var_index(db, 0, &mut msb, &mut lsb),
            -1,
            "invalid references must fail closed"
        );
        wellen_close(db);
    }

    #[test]
    fn preserves_bit_string_real_string_and_event_values() {
        let bit_path = fixture("scope_with_comment.vcd.fst");
        let bit_db = wellen_open(bit_path.as_ptr());
        let bit_signal = first_signal_with_encoding(bit_db, WellenSignalEncoding::BitVector);
        let (start, elements) = first_offset(bit_db, bit_signal);
        assert!(elements > 0);
        let (encoding, text, _) = read_typed(bit_db, bit_signal, start, 0);
        assert_eq!(encoding, WellenSignalEncoding::BitVector);
        assert!(text.iter().all(|value| b"01xzhuwl-".contains(value)));
        wellen_close(bit_db);

        let string_path = fixture("nvc/shortstring.fst");
        let string_db = wellen_open(string_path.as_ptr());
        let string_signal = first_signal_with_encoding(string_db, WellenSignalEncoding::String);
        let (start, elements) = first_offset(string_db, string_signal);
        assert!(elements >= 2);
        let (encoding, text, _) = read_typed(string_db, string_signal, start, 1);
        assert_eq!(encoding, WellenSignalEncoding::String);
        assert_eq!(
            String::from_utf8(text).unwrap(),
            "En lång röd räv                                   "
        );
        wellen_close(string_db);

        let real_path = fixture("verilator/many_sv_datatypes.fst");
        let real_db = wellen_open(real_path.as_ptr());
        let real_signal = first_signal_with_encoding(real_db, WellenSignalEncoding::Real);
        let (start, elements) = first_offset(real_db, real_signal);
        assert!(elements > 0);
        let (encoding, text, real) = read_typed(real_db, real_signal, start, 0);
        assert_eq!(encoding, WellenSignalEncoding::Real);
        assert!(text.is_empty());
        assert!(real.is_finite());
        wellen_close(real_db);

        let event_path = fixture("icarus/pull_67_event_example.fst");
        let event_db = wellen_open(event_path.as_ptr());
        let event_signal = first_signal_with_encoding(event_db, WellenSignalEncoding::Event);
        let (start, elements) = first_offset(event_db, event_signal);
        assert!(elements > 0);
        let (encoding, text, _) = read_typed(event_db, event_signal, start, 0);
        assert_eq!(encoding, WellenSignalEncoding::Event);
        assert!(text.is_empty());
        wellen_close(event_db);
    }
}

// ── FFI: Core queries ──

#[unsafe(no_mangle)]
pub extern "C" fn wellen_signal_offset_at(
    db: *const WellenDb,
    signal_ref: u32,
    time_idx: u32,
    out_start: *mut u32,
    out_elements: *mut u16,
    out_time_match: *mut i32,
    out_next_idx: *mut u32,
    out_has_next: *mut i32,
) -> i32 {
    if db.is_null()
        || out_start.is_null()
        || out_elements.is_null()
        || out_time_match.is_null()
        || out_next_idx.is_null()
        || out_has_next.is_null()
    {
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
    let offset = match signal.get_offset(time_idx) {
        Some(o) => o,
        None => return -1,
    };
    unsafe {
        *out_start = offset.start as u32;
        *out_elements = offset.elements;
        *out_time_match = offset.time_match as i32;
        match offset.next_index {
            Some(ni) => {
                *out_next_idx = ni.get();
                *out_has_next = 1;
            }
            None => {
                *out_next_idx = 0;
                *out_has_next = 0;
            }
        }
    }
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_signal_value_at_offset(
    db: *const WellenDb,
    signal_ref: u32,
    start: u32,
    element: u16,
    out_bytes: *mut u8,
    out_len: *mut u32,
) -> i32 {
    if db.is_null() || out_bytes.is_null() || out_len.is_null() {
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
    let value = signal
        .data()
        .get_value_at(start as usize + element as usize);
    let out = unsafe { std::slice::from_raw_parts_mut(out_bytes, 8) };

    match value {
        SignalValueRef::BitVec(bv) => {
            if let Some(data) = bv.be_bytes() {
                let len = data.len().min(8);
                out[..len].copy_from_slice(&data[..len]);
                unsafe { *out_len = len as u32 };
            } else {
                unsafe { *out_len = 0 };
            }
        }
        SignalValueRef::String(_) => {
            unsafe { *out_len = 0 };
            return -2;
        }
        SignalValueRef::Real(r) => {
            out.copy_from_slice(&r.to_le_bytes());
            unsafe { *out_len = 8 };
        }
        SignalValueRef::Event => {
            unsafe { *out_len = 0 };
        }
    }
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn wellen_values_at(
    db: *const WellenDb,
    refs: *const u32,
    count: u32,
    time_idx: u32,
    out_values: *mut u8,
    out_found: *mut i32,
    value_stride: u32,
) -> i32 {
    if db.is_null() || refs.is_null() || out_values.is_null() || out_found.is_null() {
        return -1;
    }
    let db = unsafe { &*db };
    let refs_slice = unsafe { std::slice::from_raw_parts(refs, count as usize) };
    let out_found_slice = unsafe { std::slice::from_raw_parts_mut(out_found, count as usize) };
    let mut found = 0i32;

    for (i, &r) in refs_slice.iter().enumerate() {
        let sr = match sig_from_u32(r) {
            Some(s) => s,
            None => {
                out_found_slice[i] = 0;
                continue;
            }
        };
        let signal = match db.wave.get_signal(sr) {
            Some(s) => s,
            None => {
                out_found_slice[i] = 0;
                continue;
            }
        };
        let offset = match signal.get_offset(time_idx) {
            Some(o) => o,
            None => {
                out_found_slice[i] = 0;
                continue;
            }
        };
        let value = signal.get_value_at(&offset, 0);
        let out_ptr = unsafe { out_values.add(i as usize * value_stride as usize) };

        match value {
            SignalValueRef::BitVec(bv) => {
                if let Some(data) = bv.be_bytes() {
                    let len = data.len().min(value_stride as usize);
                    unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), out_ptr, len) };
                }
                out_found_slice[i] = 1;
                found += 1;
            }
            SignalValueRef::Real(r) => {
                let len = 8.min(value_stride as usize);
                unsafe { std::ptr::copy_nonoverlapping(r.to_le_bytes().as_ptr(), out_ptr, len) };
                out_found_slice[i] = 1;
                found += 1;
            }
            SignalValueRef::Event => {
                out_found_slice[i] = 1;
                found += 1;
            }
            SignalValueRef::String(_) => {
                out_found_slice[i] = 0;
            }
        }
    }
    found
}
