//! Type definitions for VDF values.

use alloc::borrow::Cow;
use indexmap::IndexMap;

use super::hasher::DefaultHashBuilder;

/// A key in VDF - zero-copy when possible
pub(crate) type Key<'text> = Cow<'text, str>;

/// VDF Value - can be a string, number, object, or other types
#[derive(Clone, Debug, PartialEq)]
pub enum Value<'text> {
    /// A string value (text format and WideString from binary)
    Str(Cow<'text, str>),
    /// An object containing nested key-value pairs
    Obj(Obj<'text>),
    /// A 32-bit signed integer (binary Int32 type)
    I32(i32),
    /// A 64-bit unsigned integer (binary UInt64 type)
    U64(u64),
    /// A 32-bit float (binary Float type)
    Float(f32),
    /// A pointer value (binary Ptr type, stored as u32)
    Pointer(u32),
    /// A color value (binary Color type, RGBA)
    Color([u8; 4]),
}

impl<'text> Value<'text> {
    /// Returns `true` if this value is a string.
    pub fn is_str(&self) -> bool {
        matches!(self, Value::Str(_))
    }

    /// Returns `true` if this value is an object.
    pub fn is_obj(&self) -> bool {
        matches!(self, Value::Obj(_))
    }

    /// Returns `true` if this value is an i32.
    pub fn is_i32(&self) -> bool {
        matches!(self, Value::I32(_))
    }

    /// Returns `true` if this value is a u64.
    pub fn is_u64(&self) -> bool {
        matches!(self, Value::U64(_))
    }

    /// Returns `true` if this value is a float.
    pub fn is_float(&self) -> bool {
        matches!(self, Value::Float(_))
    }

    /// Returns `true` if this value is a pointer.
    pub fn is_pointer(&self) -> bool {
        matches!(self, Value::Pointer(_))
    }

    /// Returns `true` if this value is a color.
    pub fn is_color(&self) -> bool {
        matches!(self, Value::Color(_))
    }

    /// Returns a reference to the string value if this is a string.
    pub fn as_str(&self) -> Option<&str> {
        match self {
            Value::Str(s) => Some(s.as_ref()),
            _ => None,
        }
    }

    /// Returns a reference to the object if this is an object.
    pub fn as_obj(&self) -> Option<&Obj<'text>> {
        match self {
            Value::Obj(obj) => Some(obj),
            _ => None,
        }
    }

    /// Returns a mutable reference to the object if this is an object.
    pub fn as_obj_mut(&mut self) -> Option<&mut Obj<'text>> {
        match self {
            Value::Obj(obj) => Some(obj),
            _ => None,
        }
    }

    /// Returns the i32 value if this is an i32.
    pub fn as_i32(&self) -> Option<i32> {
        match self {
            Value::I32(n) => Some(*n),
            _ => None,
        }
    }

    /// Returns the u64 value if this is a u64.
    pub fn as_u64(&self) -> Option<u64> {
        match self {
            Value::U64(n) => Some(*n),
            _ => None,
        }
    }

    /// Returns the float value if this is a float.
    pub fn as_float(&self) -> Option<f32> {
        match self {
            Value::Float(n) => Some(*n),
            _ => None,
        }
    }

    /// Returns the pointer value if this is a pointer.
    pub fn as_pointer(&self) -> Option<u32> {
        match self {
            Value::Pointer(n) => Some(*n),
            _ => None,
        }
    }

    /// Returns the color value if this is a color.
    pub fn as_color(&self) -> Option<[u8; 4]> {
        match self {
            Value::Color(c) => Some(*c),
            _ => None,
        }
    }

    /// Returns a reference to a nested value by key.
    ///
    /// Shorthand for `self.as_obj()?.get(key)`.
    pub fn get(&self, key: &str) -> Option<&Value<'text>> {
        self.as_obj()?.get(key)
    }

    /// Traverse nested objects by path.
    ///
    /// Returns `None` if any segment doesn't exist or isn't an object.
    pub fn get_path(&self, path: &[&str]) -> Option<&Value<'text>> {
        let mut current = self;
        for key in path {
            current = current.get(key)?;
        }
        Some(current)
    }

    /// Get a string at the given path.
    pub fn get_str(&self, path: &[&str]) -> Option<&str> {
        self.get_path(path)?.as_str()
    }

    /// Get an object at the given path.
    pub fn get_obj(&self, path: &[&str]) -> Option<&Obj<'text>> {
        self.get_path(path)?.as_obj()
    }

    /// Get an i32 at the given path.
    pub fn get_i32(&self, path: &[&str]) -> Option<i32> {
        self.get_path(path)?.as_i32()
    }

    /// Get a u64 at the given path.
    pub fn get_u64(&self, path: &[&str]) -> Option<u64> {
        self.get_path(path)?.as_u64()
    }

    /// Get a float at the given path.
    pub fn get_float(&self, path: &[&str]) -> Option<f32> {
        self.get_path(path)?.as_float()
    }
}

/// Object - map from keys to values
///
/// Uses `IndexMap` for O(1) lookup while preserving insertion order.
/// Binary VDF doesn't have duplicate keys, and for text VDF we use
/// "last value wins" semantics.
#[derive(Clone, Debug, PartialEq)]
pub struct Obj<'text> {
    pub(crate) inner: IndexMap<Key<'text>, Value<'text>, DefaultHashBuilder>,
}

impl<'text> Obj<'text> {
    /// Creates a new empty VDF object.
    pub fn new() -> Self {
        Self {
            inner: IndexMap::with_hasher(DefaultHashBuilder::default()),
        }
    }

    /// Returns the number of key-value pairs in the object.
    pub fn len(&self) -> usize {
        self.inner.len()
    }

    /// Returns `true` if the object contains no key-value pairs.
    pub fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }

    /// Returns a reference to the value corresponding to the key.
    pub fn get(&self, key: &str) -> Option<&Value<'text>> {
        self.inner.get(key)
    }

    /// Returns an iterator over the key-value pairs.
    pub fn iter(&self) -> impl Iterator<Item = (&Key<'text>, &Value<'text>)> {
        self.inner.iter()
    }

    /// Returns an iterator over the keys.
    pub fn keys(&self) -> impl Iterator<Item = &str> {
        self.inner.keys().map(|k| k.as_ref())
    }

    /// Returns an iterator over the values.
    pub fn values(&self) -> impl Iterator<Item = &Value<'text>> {
        self.inner.values()
    }

    /// Returns `true` if the object contains the given key.
    pub fn contains_key(&self, key: &str) -> bool {
        self.inner.contains_key(key)
    }

    /// Returns a mutable reference to the value corresponding to the key.
    pub fn get_mut(&mut self, key: &str) -> Option<&mut Value<'text>> {
        self.inner.get_mut(key)
    }

    /// Inserts a key-value pair into the object.
    ///
    /// Returns the previous value if one existed for this key.
    pub fn insert(
        &mut self,
        key: impl Into<Key<'text>>,
        value: Value<'text>,
    ) -> Option<Value<'text>> {
        self.inner.insert(key.into(), value)
    }

    /// Removes a key from the object, preserving insertion order.
    ///
    /// This is O(n) as it shifts subsequent elements. Use [`swap_remove`](Self::swap_remove)
    /// for O(1) removal when order doesn't matter.
    ///
    /// Returns the value if the key was present.
    pub fn remove(&mut self, key: &str) -> Option<Value<'text>> {
        self.inner.shift_remove(key)
    }

    /// Removes a key from the object by swapping with the last element.
    ///
    /// This is O(1) but does not preserve insertion order.
    /// Use [`remove`](Self::remove) if order preservation is needed.
    ///
    /// Returns the value if the key was present.
    pub fn swap_remove(&mut self, key: &str) -> Option<Value<'text>> {
        self.inner.swap_remove(key)
    }
}

/// Top-level VDF document
///
/// A VDF document is essentially a single key-value pair at the root level.
#[derive(Clone, Debug, PartialEq)]
pub struct Vdf<'text> {
    key: Key<'text>,
    value: Value<'text>,
}

impl<'text> Vdf<'text> {
    /// Creates a new VDF document.
    pub fn new(key: impl Into<Key<'text>>, value: Value<'text>) -> Self {
        Self {
            key: key.into(),
            value,
        }
    }

    /// Returns the root key.
    pub fn key(&self) -> &str {
        &self.key
    }

    /// Returns a reference to the root value.
    pub fn value(&self) -> &Value<'text> {
        &self.value
    }

    /// Returns a mutable reference to the root value.
    pub fn value_mut(&mut self) -> &mut Value<'text> {
        &mut self.value
    }

    /// Consumes the Vdf and returns its parts.
    pub fn into_parts(self) -> (Cow<'text, str>, Value<'text>) {
        (self.key, self.value)
    }

    /// Returns `true` if the root value is an object.
    pub fn is_obj(&self) -> bool {
        self.value.is_obj()
    }

    /// Returns a reference to the root object if it is one.
    pub fn as_obj(&self) -> Option<&Obj<'text>> {
        self.value.as_obj()
    }

    /// Returns a reference to a nested value by key.
    pub fn get(&self, key: &str) -> Option<&Value<'text>> {
        self.value.get(key)
    }

    /// Traverse nested objects by path from the root value.
    pub fn get_path(&self, path: &[&str]) -> Option<&Value<'text>> {
        self.value.get_path(path)
    }

    /// Get a string at the given path.
    pub fn get_str(&self, path: &[&str]) -> Option<&str> {
        self.value.get_str(path)
    }

    /// Get an object at the given path.
    pub fn get_obj(&self, path: &[&str]) -> Option<&Obj<'text>> {
        self.value.get_obj(path)
    }

    /// Get an i32 at the given path.
    pub fn get_i32(&self, path: &[&str]) -> Option<i32> {
        self.value.get_i32(path)
    }

    /// Get a u64 at the given path.
    pub fn get_u64(&self, path: &[&str]) -> Option<u64> {
        self.value.get_u64(path)
    }

    /// Get a float at the given path.
    pub fn get_float(&self, path: &[&str]) -> Option<f32> {
        self.value.get_float(path)
    }
}
