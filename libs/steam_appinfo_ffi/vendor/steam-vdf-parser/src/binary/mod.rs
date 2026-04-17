//! Binary VDF format parser.
//!
//! Supports Steam's binary VDF formats:
//! - shortcuts.vdf format (simple binary)
//! - appinfo.vdf format (with optional string table)

mod byte_reader;
mod parser;
mod types;

pub use byte_reader::{read_u32_le, read_u32_le_at, read_u64_le, read_u64_le_at};
pub use parser::{parse, parse_appinfo, parse_packageinfo, parse_shortcuts};
pub use types::{APPINFO_MAGIC_40, APPINFO_MAGIC_41, BinaryType};
