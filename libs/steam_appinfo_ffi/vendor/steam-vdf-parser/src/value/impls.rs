//! Trait implementations for VDF types.

use alloc::borrow::Cow;
use alloc::string::String;

use super::types::{Key, Obj, Value, Vdf};
use core::fmt;
use core::fmt::Write as _;
use core::ops::Index;

// ============================================================================
// Pretty-print helper functions
// ============================================================================

/// Write a quoted and escaped string to the formatter.
///
/// Escapes special characters: `\n`, `\t`, `\r`, `\\`, `"`
fn write_quoted_str(f: &mut fmt::Formatter<'_>, s: &str) -> fmt::Result {
    f.write_char('"')?;
    for c in s.chars() {
        match c {
            '\n' => f.write_str("\\n")?,
            '\t' => f.write_str("\\t")?,
            '\r' => f.write_str("\\r")?,
            '\\' => f.write_str("\\\\")?,
            '"' => f.write_str("\\\"")?,
            c => f.write_char(c)?,
        }
    }
    f.write_char('"')
}

/// Write indentation (tabs) to the formatter.
fn write_indent(f: &mut fmt::Formatter<'_>, level: usize) -> fmt::Result {
    for _ in 0..level {
        f.write_char('\t')?;
    }
    Ok(())
}

/// Helper struct for pretty-printing an Obj with a specific indent level.
struct PrettyObj<'a, 'text> {
    obj: &'a Obj<'text>,
    indent: usize,
}

impl fmt::Display for PrettyObj<'_, '_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "{{")?;
        for (key, value) in self.obj.inner.iter() {
            write_indent(f, self.indent + 1)?;
            write_quoted_str(f, key)?;
            if let Value::Obj(inner_obj) = value {
                // Object value: key on one line, value (with braces) on next lines
                writeln!(f)?;
                write_indent(f, self.indent + 1)?;
                write!(
                    f,
                    "{}",
                    PrettyObj {
                        obj: inner_obj,
                        indent: self.indent + 1
                    }
                )?;
                writeln!(f)?;
            } else {
                // Scalar value: key<tab>value on same line
                write!(f, "\t")?;
                write!(f, "{:#}", value)?;
                writeln!(f)?;
            }
        }
        write_indent(f, self.indent)?;
        write!(f, "}}")
    }
}

// ============================================================================
// From implementations for Value
// ============================================================================

impl<'text> From<&'text str> for Value<'text> {
    fn from(s: &'text str) -> Self {
        Value::Str(Cow::Borrowed(s))
    }
}

impl From<String> for Value<'static> {
    fn from(s: String) -> Self {
        Value::Str(Cow::Owned(s))
    }
}

impl From<i32> for Value<'static> {
    fn from(n: i32) -> Self {
        Value::I32(n)
    }
}

impl From<u64> for Value<'static> {
    fn from(n: u64) -> Self {
        Value::U64(n)
    }
}

impl From<f32> for Value<'static> {
    fn from(n: f32) -> Self {
        Value::Float(n)
    }
}

impl From<u32> for Value<'static> {
    fn from(n: u32) -> Self {
        Value::Pointer(n)
    }
}

impl From<[u8; 4]> for Value<'static> {
    fn from(color: [u8; 4]) -> Self {
        Value::Color(color)
    }
}

impl<'text> From<Obj<'text>> for Value<'text> {
    fn from(obj: Obj<'text>) -> Self {
        Value::Obj(obj)
    }
}

impl<'text> fmt::Display for Value<'text> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Value::Str(s) => write_quoted_str(f, s),
            Value::Obj(obj) => write!(f, "{}", obj),
            Value::I32(n) => write!(f, "\"{}\"", n),
            Value::U64(n) => write!(f, "\"{}\"", n),
            Value::Float(n) => write!(f, "\"{}\"", n),
            Value::Pointer(n) => write!(f, "\"0x{:08x}\"", n),
            Value::Color(c) => write!(f, "\"{} {} {} {}\"", c[0], c[1], c[2], c[3]),
        }
    }
}

impl<'text> Default for Obj<'text> {
    fn default() -> Self {
        Self::new()
    }
}

impl<'text> fmt::Display for Obj<'text> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            PrettyObj {
                obj: self,
                indent: 0
            }
        )
    }
}

impl<'text> fmt::Display for Vdf<'text> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write_quoted_str(f, self.key())?;
        writeln!(f)?;
        write!(f, "{}", self.value())
    }
}

// ============================================================================
// IntoIterator implementations for Obj
// ============================================================================

impl<'text> IntoIterator for Obj<'text> {
    type Item = (Key<'text>, Value<'text>);
    type IntoIter = indexmap::map::IntoIter<Key<'text>, Value<'text>>;

    fn into_iter(self) -> Self::IntoIter {
        self.inner.into_iter()
    }
}

impl<'a, 'text> IntoIterator for &'a Obj<'text> {
    type Item = (&'a Key<'text>, &'a Value<'text>);
    type IntoIter = indexmap::map::Iter<'a, Key<'text>, Value<'text>>;

    fn into_iter(self) -> Self::IntoIter {
        self.inner.iter()
    }
}

// ============================================================================
// Index implementations for Value and Obj
// ============================================================================

impl<'text> Index<&str> for Value<'text> {
    type Output = Value<'text>;

    /// Returns a reference to the value at the given key.
    ///
    /// # Panics
    ///
    /// Panics if this is not an object or if the key doesn't exist.
    /// Use `get()` for non-panicking access.
    fn index(&self, key: &str) -> &Self::Output {
        self.get(key).expect("key not found in Value")
    }
}

impl<'text> Index<&str> for Obj<'text> {
    type Output = Value<'text>;

    /// Returns a reference to the value at the given key.
    ///
    /// # Panics
    ///
    /// Panics if the key doesn't exist. Use `get()` for non-panicking access.
    fn index(&self, key: &str) -> &Self::Output {
        self.get(key).expect("key not found in Obj")
    }
}
