//! Type definitions for binary VDF format.

/// Binary type byte values used in Steam's binary VDF format.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum BinaryType {
    /// Nested object start marker.
    ///
    /// Named `None` to match Steam SDK's `TYPE_NONE`, which indicates a subsection
    /// with child keys rather than a leaf value. In binary VDF, `0x00` followed by
    /// a key name starts a new nested object that continues until `ObjectEnd` (0x08).
    None = 0x00,
    /// String value (null-terminated).
    String = 0x01,
    /// 32-bit integer value.
    Int32 = 0x02,
    /// 32-bit float value.
    Float = 0x03,
    /// Pointer value.
    Ptr = 0x04,
    /// Wide string value (UTF-16).
    WString = 0x05,
    /// Color value (RGBA).
    Color = 0x06,
    /// 64-bit unsigned integer value.
    UInt64 = 0x07,
    /// End of object marker.
    ObjectEnd = 0x08,
}

impl BinaryType {
    /// Attempts to convert a byte to a `BinaryType`.
    ///
    /// Returns `None` if the byte doesn't correspond to a known type.
    #[inline]
    pub fn from_byte(b: u8) -> Option<Self> {
        match b {
            0x00 => Some(BinaryType::None),
            0x01 => Some(BinaryType::String),
            0x02 => Some(BinaryType::Int32),
            0x03 => Some(BinaryType::Float),
            0x04 => Some(BinaryType::Ptr),
            0x05 => Some(BinaryType::WString),
            0x06 => Some(BinaryType::Color),
            0x07 => Some(BinaryType::UInt64),
            0x08 => Some(BinaryType::ObjectEnd),
            _ => None,
        }
    }
}

/// Magic number for appinfo.vdf format version 40.
///
/// This format uses null-terminated UTF-8 keys.
pub const APPINFO_MAGIC_40: u32 = 0x07564428;

/// Magic number for appinfo.vdf format version 41 (with string table).
///
/// This format uses u32 indices into a string table for keys, enabling O(1) lookups.
pub const APPINFO_MAGIC_41: u32 = 0x07564429;

/// Magic base for packageinfo.vdf format (upper 3 bytes).
pub const PACKAGEINFO_MAGIC_BASE: u32 = 0x065655;

/// Magic number for packageinfo.vdf format version 39.
pub const PACKAGEINFO_MAGIC_39: u32 = 0x06565527;

/// Magic number for packageinfo.vdf format version 40 (with PICS token).
pub const PACKAGEINFO_MAGIC_40: u32 = 0x06565528;
