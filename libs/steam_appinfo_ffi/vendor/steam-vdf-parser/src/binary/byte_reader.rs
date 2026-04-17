//! Utilities for reading little-endian values from byte slices.

/// Reads a little-endian u32 from a byte slice.
///
/// Returns `None` if the slice doesn't have enough bytes.
///
/// # Examples
/// ```
/// use steam_vdf_parser::binary::read_u32_le;
///
/// let data = [0x01, 0x02, 0x03, 0x04];
/// assert_eq!(read_u32_le(&data), Some(0x04030201));
/// assert_eq!(read_u32_le(&[0x01, 0x02]), None);
/// ```
#[inline]
pub fn read_u32_le(input: &[u8]) -> Option<u32> {
    input.get(..4).and_then(|bytes| {
        let arr: [u8; 4] = bytes.try_into().ok()?;
        Some(u32::from_le_bytes(arr))
    })
}

/// Reads a little-endian u64 from a byte slice.
///
/// Returns `None` if the slice doesn't have enough bytes.
///
/// # Examples
/// ```
/// use steam_vdf_parser::binary::read_u64_le;
///
/// let data = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
/// assert_eq!(read_u64_le(&data), Some(0x0807060504030201));
/// assert_eq!(read_u64_le(&[0x01, 0x02]), None);
/// ```
#[inline]
pub fn read_u64_le(input: &[u8]) -> Option<u64> {
    input.get(..8).and_then(|bytes| {
        let arr: [u8; 8] = bytes.try_into().ok()?;
        Some(u64::from_le_bytes(arr))
    })
}

/// Reads a little-endian u32 from a byte slice at a specific offset.
///
/// Returns `None` if the slice doesn't have enough bytes from the offset.
///
/// # Examples
/// ```
/// use steam_vdf_parser::binary::read_u32_le_at;
///
/// let data = [0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04];
/// assert_eq!(read_u32_le_at(&data, 2), Some(0x04030201));
/// assert_eq!(read_u32_le_at(&data, 4), None);
/// ```
#[inline]
pub fn read_u32_le_at(input: &[u8], offset: usize) -> Option<u32> {
    input.get(offset..offset + 4).and_then(|bytes| {
        let arr: [u8; 4] = bytes.try_into().ok()?;
        Some(u32::from_le_bytes(arr))
    })
}

/// Reads a little-endian u64 from a byte slice at a specific offset.
///
/// Returns `None` if the slice doesn't have enough bytes from the offset.
///
/// # Examples
/// ```
/// use steam_vdf_parser::binary::read_u64_le_at;
///
/// let data = [0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
/// assert_eq!(read_u64_le_at(&data, 4), Some(0x0807060504030201));
/// assert_eq!(read_u64_le_at(&data, 8), None);
/// ```
#[inline]
pub fn read_u64_le_at(input: &[u8], offset: usize) -> Option<u64> {
    input.get(offset..offset + 8).and_then(|bytes| {
        let arr: [u8; 8] = bytes.try_into().ok()?;
        Some(u64::from_le_bytes(arr))
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_read_u32_le() {
        let data = [0x01, 0x02, 0x03, 0x04];
        assert_eq!(read_u32_le(&data), Some(0x04030201));
    }

    #[test]
    fn test_read_u32_le_short() {
        assert_eq!(read_u32_le(&[0x01, 0x02]), None);
        assert_eq!(read_u32_le(&[]), None);
    }

    #[test]
    fn test_read_u64_le() {
        let data = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
        assert_eq!(read_u64_le(&data), Some(0x0807060504030201));
    }

    #[test]
    fn test_read_u64_le_short() {
        assert_eq!(read_u64_le(&[0x01, 0x02, 0x03, 0x04]), None);
        assert_eq!(read_u64_le(&[]), None);
    }

    #[test]
    fn test_read_u32_le_at() {
        let data = [0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04];
        assert_eq!(read_u32_le_at(&data, 0), Some(0x0201FFFF));
        assert_eq!(read_u32_le_at(&data, 2), Some(0x04030201));
    }

    #[test]
    fn test_read_u32_le_at_out_of_bounds() {
        let data = [0x01, 0x02, 0x03, 0x04];
        assert_eq!(read_u32_le_at(&data, 1), None);
        assert_eq!(read_u32_le_at(&data, 4), None);
    }

    #[test]
    fn test_read_u64_le_at() {
        let data = [0xFF; 12];
        assert_eq!(read_u64_le_at(&data, 0), Some(0xFFFFFFFFFFFFFFFF));
        assert_eq!(read_u64_le_at(&data, 4), Some(0xFFFFFFFFFFFFFFFF));
    }

    #[test]
    fn test_read_u64_le_at_out_of_bounds() {
        let data = [0x01; 8];
        assert_eq!(read_u64_le_at(&data, 1), None);
        assert_eq!(read_u64_le_at(&data, 8), None);
    }
}
