//! Blazing fast VDF (Valve Data Format) parser.
//!
//! This library provides parsers for both text and binary VDF formats used by Steam and
//! other Valve Software products.
//!
//! # Features
//!
//! - **`no_std` compatible** — works without the standard library, requires only `alloc`
//! - **Zero-copy parsing** for text format when possible (no escape sequences)
//! - **Binary format support** for Steam's appinfo.vdf, shortcuts.vdf, and packageinfo.vdf
//! - **Winnow-powered** text parser for maximum performance
//!
//! # Example
//!
//! ```
//! use steam_vdf_parser::parse_text;
//!
//! let input = r#""root"
//! {
//!     "key" "value"
//!     "nested"
//!     {
//!         "subkey" "subvalue"
//!     }
//! }"#;
//!
//! let vdf = parse_text(input).unwrap();
//! ```

#![no_std]
#![warn(missing_docs)]

extern crate alloc;

use alloc::borrow::Cow;
use alloc::string::ToString;

pub mod binary;
pub mod error;
pub mod text;
pub mod value;

pub use error::{Error, Result};
pub use value::{Obj, Value, Vdf};

// Re-export commonly used functions
pub use text::parse_text;

/// Parse VDF from binary format (autodetects shortcuts or appinfo format).
///
/// This function returns zero-copy data where possible - strings are borrowed
/// from the input buffer. Use `.into_owned()` to convert to an owned `Vdf<'static>`.
pub fn parse_binary(input: &[u8]) -> Result<Vdf<'_>> {
    binary::parse(input)
}

/// Parse a shortcuts.vdf format binary file.
///
/// This function returns zero-copy data where possible - strings are borrowed
/// from the input buffer. Use `.into_owned()` to convert to an owned `Vdf<'static>`.
pub fn parse_shortcuts(input: &[u8]) -> Result<Vdf<'_>> {
    binary::parse_shortcuts(input)
}

/// Parse an appinfo.vdf format binary file.
///
/// This function returns zero-copy data where possible - strings are borrowed
/// from the input buffer. Use `.into_owned()` to convert to an owned `Vdf<'static>`.
pub fn parse_appinfo(input: &[u8]) -> Result<Vdf<'_>> {
    binary::parse_appinfo(input)
}

/// Parse a packageinfo.vdf format binary file.
///
/// This function returns zero-copy data where possible - strings are borrowed
/// from the input buffer. Use `.into_owned()` to convert to an owned `Vdf<'static>`.
pub fn parse_packageinfo(input: &[u8]) -> Result<Vdf<'_>> {
    binary::parse_packageinfo(input)
}

// Convert from borrowed to owned
impl Vdf<'_> {
    /// Convert to an owned version (with 'static lifetime).
    ///
    /// This creates a new `Vdf<'static>` with all strings owned, allowing the
    /// data to outlive the original input.
    pub fn into_owned(self) -> Vdf<'static> {
        let (key, value) = self.into_parts();
        let owned_key: Cow<'static, str> = match key {
            Cow::Borrowed(s) => Cow::Owned(s.to_string()),
            Cow::Owned(s) => Cow::Owned(s),
        };
        Vdf::new(owned_key, value.into_owned())
    }
}

impl Value<'_> {
    /// Convert to an owned version (with 'static lifetime).
    pub fn into_owned(self) -> Value<'static> {
        match self {
            Value::Str(s) => Value::Str(match s {
                Cow::Borrowed(b) => b.to_string().into(),
                Cow::Owned(o) => o.into(),
            }),
            Value::Obj(obj) => Value::Obj(obj.into_owned()),
            Value::I32(n) => Value::I32(n),
            Value::U64(n) => Value::U64(n),
            Value::Float(n) => Value::Float(n),
            Value::Pointer(n) => Value::Pointer(n),
            Value::Color(c) => Value::Color(c),
        }
    }
}

impl Obj<'_> {
    /// Convert to an owned version (with 'static lifetime).
    pub fn into_owned(self) -> Obj<'static> {
        let mut new = Obj::new();
        for (k, v) in self.iter() {
            let owned_key: Cow<'static, str> = match k {
                Cow::Borrowed(b) => Cow::Owned(b.to_string()),
                Cow::Owned(o) => Cow::Owned(o.clone()),
            };
            // Clone the value since we're iterating
            new.insert(owned_key, v.clone().into_owned());
        }
        new
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;

    // Simple shortcuts.vdf format test data (object start, key, string value, object end)
    const SHORTCUTS_VDF: &[u8] = &[
        0x00, // Object start
        b't', b'e', b's', b't', 0x00, // Key "test"
        0x01, // String type
        b'k', b'e', b'y', 0x00, // Nested key "key"
        b'v', b'a', b'l', b'u', b'e', 0x00, // Value "value"
        0x08, // Object end
    ];

    #[test]
    fn test_parse_binary() {
        let vdf = parse_binary(SHORTCUTS_VDF).unwrap();
        assert!(vdf.as_obj().is_some());
        assert_eq!(vdf.key(), "root");
        let obj = vdf.as_obj().unwrap();
        let test_obj = obj.get("test").and_then(|v| v.as_obj()).unwrap();
        assert_eq!(test_obj.get("key").and_then(|v| v.as_str()), Some("value"));
    }

    #[test]
    fn test_parse_shortcuts() {
        let vdf = parse_shortcuts(SHORTCUTS_VDF).unwrap();
        assert!(vdf.as_obj().is_some());
        assert_eq!(vdf.key(), "root");
        let obj = vdf.as_obj().unwrap();
        let test_obj = obj.get("test").and_then(|v| v.as_obj()).unwrap();
        assert_eq!(test_obj.get("key").and_then(|v| v.as_str()), Some("value"));
    }

    #[test]
    fn test_parse_appinfo() {
        // appinfo v40 magic + terminator (no apps)
        // Need 8 bytes (magic + universe) + 68 bytes (APPINFO_ENTRY_HEADER_SIZE) = 76 bytes total
        let mut input = vec![
            0x28, 0x44, 0x56, 0x07, // magic: 0x07564428 (APPINFO_MAGIC_40)
            0x20, 0x00, 0x00, 0x00, // universe: 32
            0x00, 0x00, 0x00, 0x00, // app_id = 0 (terminator)
        ];
        // Pad to 76 bytes total (8 + APPINFO_ENTRY_HEADER_SIZE)
        input.resize(76, 0);
        let result = parse_appinfo(&input);
        if let Err(e) = &result {
            panic!("parse_appinfo failed: {:?}", e);
        }
        assert!(result.is_ok());
        let vdf = result.unwrap();
        assert!(vdf.key().starts_with("appinfo_universe_"));
    }

    #[test]
    fn test_into_owned_vdf() {
        let input = r#""root"
        {
            "key" "value"
        }"#;
        let borrowed = parse_text(input).unwrap();
        let owned = borrowed.into_owned();
        assert_eq!(owned.key(), "root");
    }

    #[test]
    fn test_into_owned_value_str() {
        let borrowed = Value::Str("test".into());
        let owned = borrowed.into_owned();
        assert!(matches!(owned, Value::Str(Cow::Owned(_))));
    }

    #[test]
    fn test_into_owned_value_obj() {
        let mut obj = Obj::new();
        obj.insert("key", Value::Str("value".into()));
        let borrowed = Value::Obj(obj);
        let owned = borrowed.into_owned();
        assert!(matches!(owned, Value::Obj(_)));
    }

    #[test]
    fn test_into_owned_obj() {
        let mut obj = Obj::new();
        obj.insert("key", Value::Str("value".into()));
        let owned = obj.into_owned();
        assert!(owned.get("key").is_some());
    }
}
