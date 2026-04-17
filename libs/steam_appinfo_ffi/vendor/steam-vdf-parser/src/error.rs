//! Error types for VDF parsing.

use alloc::format;
use alloc::string::String;
use alloc::vec::Vec;
use core::fmt;

/// Result type for VDF operations.
pub type Result<T> = core::result::Result<T, Error>;

/// Create a parse error with truncated input snippet (max 50 chars).
///
/// The snippet is limited to 50 characters to keep error messages manageable.
///
/// # Parameters
/// - `input`: The input string being parsed
/// - `offset`: Byte offset where the error occurred
/// - `context`: Description of what was expected
pub fn parse_error(input: &str, offset: usize, context: impl Into<String>) -> Error {
    let snippet = input.chars().take(50).collect::<String>();
    Error::ParseError {
        input: snippet,
        offset,
        context: context.into(),
    }
}

/// Errors that can occur during VDF parsing.
#[derive(Debug)]
pub enum Error {
    /// Binary format errors
    /// ---------------------

    /// Invalid magic number in binary VDF header.
    InvalidMagic {
        /// The magic number that was found.
        found: u32,
        /// Expected magic numbers for this format.
        expected: &'static [u32],
    },

    /// Unknown type byte encountered.
    UnknownType {
        /// The type byte that was found.
        type_byte: u8,
        /// Offset in the input where this occurred.
        offset: usize,
    },

    /// Invalid string index into the string table.
    InvalidStringIndex {
        /// The index that was requested.
        index: usize,
        /// The maximum valid index.
        max: usize,
    },

    /// Unexpected end of input while parsing.
    UnexpectedEndOfInput {
        /// Description of what was being read.
        context: &'static str,
        /// Offset in the input where this occurred.
        offset: usize,
        /// Expected minimum number of bytes.
        expected: usize,
        /// Actual number of bytes available.
        actual: usize,
    },

    /// Invalid UTF-8 sequence in binary data.
    InvalidUtf8 {
        /// Offset where the error occurred.
        offset: usize,
    },

    /// Invalid UTF-16 sequence in binary data.
    InvalidUtf16 {
        /// Offset where the error occurred.
        offset: usize,
        /// Position of the unpaired surrogate.
        position: usize,
    },

    /// Text format errors
    /// ------------------

    /// Parse error with context.
    ParseError {
        /// A snippet of the input near the error.
        input: String,
        /// Offset in the input where this occurred.
        offset: usize,
        /// Context describing what was expected.
        context: String,
    },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::InvalidMagic { found, expected } => {
                let expected_str: String = expected
                    .iter()
                    .map(|v| format!("0x{:08x}", v))
                    .collect::<Vec<_>>()
                    .join(" or ");
                write!(
                    f,
                    "Invalid magic number: expected {}, found 0x{:08x}",
                    expected_str, found
                )
            }
            Error::UnknownType { type_byte, offset } => {
                write!(
                    f,
                    "Unknown type byte 0x{:02x} at offset {}",
                    type_byte, offset
                )
            }
            Error::InvalidStringIndex { index, max } => {
                if *max == 0 {
                    write!(f, "Invalid string index {}: string table is empty", index)
                } else {
                    write!(
                        f,
                        "Invalid string index {}: string table has {} entries (valid range: 0..{})",
                        index, max, max
                    )
                }
            }
            Error::UnexpectedEndOfInput {
                context,
                offset,
                expected,
                actual,
            } => {
                write!(
                    f,
                    "Unexpected end of input at offset {} while {}: expected {} bytes, found {}",
                    offset, context, expected, actual
                )
            }
            Error::InvalidUtf8 { offset } => {
                write!(f, "Invalid UTF-8 sequence at offset {}", offset)
            }
            Error::InvalidUtf16 { offset, position } => {
                write!(
                    f,
                    "Invalid UTF-16 sequence at offset {} (surrogate position {})",
                    offset, position
                )
            }
            Error::ParseError {
                input,
                offset,
                context,
            } => {
                let snippet = if input.len() > 50 {
                    format!("{}...", &input[..50])
                } else {
                    input.clone()
                };
                write!(
                    f,
                    "Parse error at offset {}: {} (near: \"{}\")",
                    offset, context, snippet
                )
            }
        }
    }
}

impl core::error::Error for Error {}

impl Error {
    /// Adjusts the offset in error variants that contain position information.
    ///
    /// This is used to add a base offset when parsing from a sub-slice,
    /// converting relative offsets to absolute offsets in the original input.
    fn with_offset(mut self, base: usize) -> Self {
        match &mut self {
            Error::UnexpectedEndOfInput { offset, .. } => *offset += base,
            Error::InvalidUtf8 { offset } => *offset += base,
            Error::InvalidUtf16 { offset, .. } => *offset += base,
            Error::UnknownType { offset, .. } => *offset += base,
            Error::ParseError { offset, .. } => *offset += base,
            // Other variants don't have offsets to adjust
            Error::InvalidMagic { .. } | Error::InvalidStringIndex { .. } => {}
        }
        self
    }
}

/// Returns a closure that adds an offset to an error.
///
/// This is used with `.map_err()` to adjust error offsets when parsing from sub-slices.
pub(crate) fn with_offset(base: usize) -> impl Fn(Error) -> Error {
    move |err| err.with_offset(base)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::binary::{APPINFO_MAGIC_40, APPINFO_MAGIC_41};
    use alloc::format;
    use alloc::string::ToString;

    #[test]
    fn test_error_display_invalid_magic() {
        let err = Error::InvalidMagic {
            found: 0xDEADBEEF,
            expected: &[APPINFO_MAGIC_40, APPINFO_MAGIC_41],
        };
        let msg = format!("{}", err);
        assert!(msg.contains("0xdeadbeef"));
        assert!(msg.contains("0x07564428"));
    }

    #[test]
    fn test_error_display_unknown_type() {
        let err = Error::UnknownType {
            type_byte: 0xFF,
            offset: 42,
        };
        let msg = format!("{}", err);
        assert!(msg.contains("0xff"));
        assert!(msg.contains("42"));
    }

    #[test]
    fn test_error_display_invalid_string_index() {
        let err = Error::InvalidStringIndex { index: 5, max: 3 };
        let msg = format!("{}", err);
        assert!(msg.contains("5"));
        assert!(msg.contains("3"));
    }

    #[test]
    fn test_error_display_invalid_string_index_empty() {
        let err = Error::InvalidStringIndex { index: 0, max: 0 };
        let msg = format!("{}", err);
        assert!(msg.contains("empty"));
    }

    #[test]
    fn test_error_display_unexpected_end_of_input() {
        let err = Error::UnexpectedEndOfInput {
            context: "reading string",
            offset: 10,
            expected: 4,
            actual: 2,
        };
        let msg = format!("{}", err);
        assert!(msg.contains("reading string"));
        assert!(msg.contains("10"));
        assert!(msg.contains("expected 4"));
        assert!(msg.contains("found 2"));
    }

    #[test]
    fn test_error_display_invalid_utf8() {
        let err = Error::InvalidUtf8 { offset: 15 };
        let msg = format!("{}", err);
        assert!(msg.contains("15"));
    }

    #[test]
    fn test_error_display_invalid_utf16() {
        let err = Error::InvalidUtf16 {
            offset: 20,
            position: 3,
        };
        let msg = format!("{}", err);
        assert!(msg.contains("20"));
        assert!(msg.contains("3"));
    }

    #[test]
    fn test_error_display_parse_error() {
        let err = Error::ParseError {
            input: "some very long input that should be truncated in the message".to_string(),
            offset: 5,
            context: "expected quote".to_string(),
        };
        let msg = format!("{}", err);
        assert!(msg.contains("5"));
        assert!(msg.contains("expected quote"));
        // The input should be truncated to 50 chars, so the message shouldn't be too long
        assert!(msg.len() < 150);
    }

    #[test]
    fn test_error_with_offset_unexpected_end() {
        let err = Error::UnexpectedEndOfInput {
            context: "test",
            offset: 10,
            expected: 4,
            actual: 2,
        };
        let adjusted = err.with_offset(100);
        match adjusted {
            Error::UnexpectedEndOfInput { offset, .. } => {
                assert_eq!(offset, 110);
            }
            _ => panic!("Unexpected error type"),
        }
    }

    #[test]
    fn test_error_with_offset_invalid_utf8() {
        let err = Error::InvalidUtf8 { offset: 5 };
        let adjusted = err.with_offset(100);
        match adjusted {
            Error::InvalidUtf8 { offset } => {
                assert_eq!(offset, 105);
            }
            _ => panic!("Unexpected error type"),
        }
    }

    #[test]
    fn test_error_with_offset_invalid_utf16() {
        let err = Error::InvalidUtf16 {
            offset: 10,
            position: 2,
        };
        let adjusted = err.with_offset(100);
        match adjusted {
            Error::InvalidUtf16 { offset, position } => {
                assert_eq!(offset, 110);
                assert_eq!(position, 2);
            }
            _ => panic!("Unexpected error type"),
        }
    }

    #[test]
    fn test_error_with_offset_unknown_type() {
        let err = Error::UnknownType {
            type_byte: 0x42,
            offset: 7,
        };
        let adjusted = err.with_offset(100);
        match adjusted {
            Error::UnknownType { type_byte, offset } => {
                assert_eq!(type_byte, 0x42);
                assert_eq!(offset, 107);
            }
            _ => panic!("Unexpected error type"),
        }
    }

    #[test]
    fn test_error_with_offset_parse_error() {
        let err = Error::ParseError {
            input: "test".to_string(),
            offset: 3,
            context: "context".to_string(),
        };
        let adjusted = err.with_offset(100);
        match adjusted {
            Error::ParseError { offset, .. } => {
                assert_eq!(offset, 103);
            }
            _ => panic!("Unexpected error type"),
        }
    }

    #[test]
    fn test_error_with_offset_no_change_for_non_offset_variants() {
        let err = Error::InvalidMagic {
            found: 0x12345678,
            expected: &[APPINFO_MAGIC_40],
        };
        let adjusted = err.with_offset(100);
        // InvalidMagic doesn't have an offset field, so it should be unchanged
        match adjusted {
            Error::InvalidMagic { found, .. } => {
                assert_eq!(found, 0x12345678);
            }
            _ => panic!("Unexpected error type"),
        }
    }

    #[test]
    fn test_parse_error_truncates_long_input() {
        let long_input = "a".repeat(100);
        let err = parse_error(&long_input, 0, "test context");
        match err {
            Error::ParseError { input, .. } => {
                assert!(input.len() <= 50, "Input should be truncated to 50 chars");
            }
            _ => panic!("Expected ParseError variant"),
        }
    }

    #[test]
    fn test_with_offset_closure() {
        let base_offset = 100;
        let f = with_offset(base_offset);
        let err = Error::InvalidUtf8 { offset: 5 };
        let adjusted = f(err);
        match adjusted {
            Error::InvalidUtf8 { offset } => {
                assert_eq!(offset, 105);
            }
            _ => panic!("Unexpected error type"),
        }
    }
}
