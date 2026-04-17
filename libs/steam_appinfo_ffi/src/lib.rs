//! C FFI bridge to steam-vdf-parser for reading Steam's appinfo.vdf (binary,
//! including v41 / magic 0x07564429 with string table).
//!
//! Exposes a single function that streams `(appid, type, name)` tuples to a
//! C callback so the C++ side can build its own data structures without
//! needing to know the parser's internals.

use std::ffi::{CStr, CString, c_char, c_void};
use std::fs;

use steam_vdf_parser::parse_appinfo;

/// Callback invoked once per app. `user` is passed through unchanged.
/// Strings are valid for the duration of the call only — the C++ side must
/// copy whatever it wants to keep.
pub type SteamAppInfoCallback =
    extern "C" fn(user: *mut c_void, appid: u32, type_: *const c_char, name: *const c_char);

/// Parse the appinfo.vdf at `path_c` and call `cb(user, appid, type, name)`
/// for every app whose `common` section is readable.
///
/// Returns 0 on success, negative on error:
///   -1 = path is null / not valid UTF-8
///   -2 = file read error
///   -3 = parse error (unsupported magic, truncated, etc.)
///
/// # Safety
/// `path_c` must be a valid null-terminated C string. `cb` must be non-null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn steam_appinfo_parse(
    path_c: *const c_char,
    user: *mut c_void,
    cb: SteamAppInfoCallback,
) -> i32 {
    if path_c.is_null() {
        return -1;
    }
    let path = match unsafe { CStr::from_ptr(path_c) }.to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };

    let data = match fs::read(path) {
        Ok(d) => d,
        Err(_) => return -2,
    };

    let vdf = match parse_appinfo(&data) {
        Ok(v) => v.into_owned(),
        Err(_) => return -3,
    };

    let root = match vdf.as_obj() {
        Some(o) => o,
        None => return -3,
    };

    for (app_id_str, app_value) in root.iter() {
        let appid = match app_id_str.parse::<u32>() {
            Ok(v) => v,
            Err(_) => continue,
        };

        let app_obj = match app_value.as_obj() {
            Some(o) => o,
            None => continue,
        };

        let common = app_obj
            .get("appinfo")
            .and_then(|v| v.as_obj())
            .and_then(|appinfo| appinfo.get("common"))
            .and_then(|v| v.as_obj());

        let common = match common {
            Some(c) => c,
            None => continue,
        };

        let type_ = common.get("type").and_then(|v| v.as_str()).unwrap_or("");
        let name = common.get("name").and_then(|v| v.as_str()).unwrap_or("");

        let type_c = CString::new(type_).unwrap_or_default();
        let name_c = CString::new(name).unwrap_or_default();
        cb(user, appid, type_c.as_ptr(), name_c.as_ptr());
    }

    0
}
