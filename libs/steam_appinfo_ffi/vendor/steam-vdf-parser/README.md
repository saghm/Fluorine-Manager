# steam-vdf-parser

[![Crates.io](https://img.shields.io/crates/v/steam-vdf-parser.svg)](https://crates.io/crates/steam-vdf-parser)
[![Documentation](https://docs.rs/steam-vdf-parser/badge.svg)](https://docs.rs/steam-vdf-parser)
[![License](https://img.shields.io/crates/l/steam-vdf-parser.svg)](https://github.com/mexus/steam-vdf-parser#license)
[![CI](https://github.com/mexus/steam-vdf-parser/actions/workflows/ci.yml/badge.svg)](https://github.com/mexus/steam-vdf-parser/actions/workflows/ci.yml)

A blazing fast, zero-copy parser for Steam's VDF (Valve Data Format) files in Rust.

Supports both text and binary formats used by Steam, including `shortcuts.vdf`, `appinfo.vdf`, and `packageinfo.vdf`.

## Features

- **`no_std` compatible** — works without the standard library, requires only `alloc`
- **Zero-copy parsing** — text format returns borrowed strings when possible (no escape sequences)
- **Binary format support** — parses all Steam binary VDF variants
- **Version-aware** — handles `appinfo.vdf` v40 (null-terminated keys) and v41 (string table)
- **`serde`-free** — simple, direct data structures with no hidden allocations

## Usage

### Text Format

```rust
use steam_vdf_parser::parse_text;

let input = r#""root"
{
    "key" "value"
    "nested"
    {
        "subkey" "subvalue"
    }
}"#;

let vdf = parse_text(input)?;
assert_eq!(vdf.key(), "root");

// Access nested values
let obj = vdf.as_obj().unwrap();
let value = obj.get("key").and_then(|v| v.as_str()).unwrap();
assert_eq!(value, "value");

// Path-based access
let subvalue = vdf.get_str(&["nested", "subkey"]).unwrap();
assert_eq!(subvalue, "subvalue");
```

### Binary Format (auto-detect)

```rust
use steam_vdf_parser::parse_binary;

let data = read_vdf_data_somehow()?;
let vdf = parse_binary(&data)?;

// For data that needs to outlive the input:
let owned = vdf.into_owned();
```

### Specific Formats

```rust
use steam_vdf_parser::{parse_appinfo, parse_packageinfo};

// appinfo.vdf (auto-detects v40/v41)
let data = read_vdf_data_somehow()?;
let vdf = parse_appinfo(&data)?;

// packageinfo.vdf
let data = read_vdf_data_somehow()?;
let vdf = parse_packageinfo(&data)?;
```

## `no_std` Support

This library is `no_std` compatible and only requires the `alloc` crate. Enable it in your `Cargo.toml`:

```toml
[dependencies]
steam-vdf-parser = "0.1"
```

For `no_std` environments, make sure to have a global allocator configured:

```rust
extern crate alloc;
use steam_vdf_parser::parse_text;

// Your parsing code here
```

## Data Structures

### `Vdf<'text>`

The top-level VDF document. A VDF document is essentially a single key-value pair at the root level.

```rust
let vdf = parse_text(input)?;

// Access root key and value
let key: &str = vdf.key();
let value: &Value = vdf.value();

// Check if root is an object
if vdf.is_obj() {
    let obj = vdf.as_obj().unwrap();
}

// Direct nested access
let nested = vdf.get("key");  // Option<&Value>

// Path-based traversal
let deep = vdf.get_path(&["nested", "deep", "value"]);
let name = vdf.get_str(&["config", "name"]);
let count = vdf.get_i32(&["settings", "count"]);
```

### `Value<'text>` Enum

| Variant | Rust Type | Type Check | Accessor |
|---------|-----------|------------|----------|
| `Str` | `Cow<'text, str>` | `is_str()` | `as_str()` |
| `Obj` | `Obj<'text>` | `is_obj()` | `as_obj()`, `as_obj_mut()` |
| `I32` | `i32` | `is_i32()` | `as_i32()` |
| `U64` | `u64` | `is_u64()` | `as_u64()` |
| `Float` | `f32` | `is_float()` | `as_float()` |
| `Pointer` | `u32` | `is_pointer()` | `as_pointer()` |
| `Color` | `[u8; 4]` | `is_color()` | `as_color()` |

Path-based access methods are also available on `Value`:

```rust
// Traverse nested objects
let deep = value.get_path(&["nested", "key"]);

// Typed path access
let name = value.get_str(&["config", "name"]);
let obj = value.get_obj(&["settings"]);
let count = value.get_i32(&["stats", "count"]);
let id = value.get_u64(&["user", "id"]);
let ratio = value.get_float(&["metrics", "ratio"]);
```

### `Obj<'text>`

A `HashMap`-backed object (using `hashbrown` for `no_std` compatibility) with O(1) lookup:

```rust
let obj = vdf.as_obj().unwrap();

// Basic access
let len = obj.len();
let is_empty = obj.is_empty();
let value = obj.get("key");           // Option<&Value>
let exists = obj.contains_key("key"); // bool

// Iteration
for (key, value) in obj.iter() {
    println!("{}: {}", key, value);
}
for key in obj.keys() {
    println!("Key: {}", key);
}
for value in obj.values() {
    println!("Value: {}", value);
}

// Mutation
let mut obj = Obj::new();
obj.insert("key", Value::Str("value".into()));
obj.get_mut("key");  // Option<&mut Value>
obj.remove("key");   // Option<Value>
```

### Lifetime and Ownership

Parsing functions return `Vdf<'_>` with strings borrowed from input where possible. Use `.into_owned()` to convert to `Vdf<'static>`:

```rust
let borrowed: Vdf<'_> = parse_text(input)?;
let owned: Vdf<'static> = borrowed.into_owned();
```

## Binary Format Reference

### Type Bytes

All binary VDF formats use type byte prefixes:

| Byte | Name | Description |
|------|------|-------------|
| `0x00` | None/Object | Start of an object (followed by key) |
| `0x01` | String | Null-terminated UTF-8 string value |
| `0x02` | Int32 | 4-byte little-endian signed integer |
| `0x03` | Float | 4-byte little-endian IEEE-754 float |
| `0x04` | Pointer | 4-byte pointer value (stored as `u32`) |
| `0x05` | WString | Null-terminated UTF-16LE string (`0x00 0x00` terminator) |
| `0x06` | Color | 4 bytes RGBA |
| `0x07` | UInt64 | 8-byte little-endian unsigned integer |
| `0x08` | ObjectEnd | End of current object |

### Entry Format

`[TypeByte] [Key] [Value...]`

- **Key encoding** varies by format (null-terminated or string table index)
- **String values** are always inline null-terminated UTF-8 (never from string table)

### shortcuts.vdf

Simple binary format. Keys are null-terminated UTF-8 strings. Ends at EOF.

### appinfo.vdf

#### File Header

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| `0x00` | 4 | Magic | `0x07564428` (v40) or `0x07564429` (v41) |
| `0x04` | 4 | Universe | Always `1` (public) |
| `0x08` | 8 | String Table Offset | Present only in v41 |

#### App Entry Header (68 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| `0x00` | 4 | App ID | Steam application ID |
| `0x04` | 4 | Size | Size of remaining data (60 bytes + VDF payload) |
| `0x08` | 4 | Info State | Flags (e.g., `2` = available) |
| `0x0C` | 4 | Last Updated | Unix timestamp |
| `0x10` | 8 | PICS Token | Access token for PICS API |
| `0x18` | 20 | SHA1 | Hash of VDF payload |
| `0x2C` | 4 | Change Number | Sequence number |
| `0x30` | 20 | Binary SHA1 | Hash of binary VDF data |

VDF data starts at offset `0x44` (68 bytes). Length = `Size - 60`.

#### Key Encoding

- **v40**: Keys are null-terminated UTF-8 strings
- **v41**: Keys are `u32` indices into the string table
- **All versions**: String *values* are always inline null-terminated UTF-8

#### String Table (v41 only)

Located at `String Table Offset`:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| `0x00` | 4 | String Count | Number of strings (little-endian) |
| `0x04` | - | Strings | Null-terminated UTF-8 strings |

### packageinfo.vdf

#### File Header

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| `0x00` | 4 | Magic | Upper 3 bytes: `0x065655`, lower byte: version (`27` = v39, `28` = v40) |
| `0x04` | 4 | Universe | Always `1` (public) |

#### Package Entry Header

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| `0x00` | 4 | Package ID | `0xFFFFFFFF` marks end of file |
| `0x04` | 20 | SHA-1 | Hash of VDF payload |
| `0x18` | 4 | Change Number | Sequence number |
| `0x1C` | 8 | PICS Token | Present only in v40 |

Followed by binary VDF blob (null-terminated keys, like shortcuts.vdf).

## Performance

Text parsing is zero-copy when strings contain no escape sequences — `Cow::Borrowed` refers directly into the input. Escape sequences and wide strings (UTF-16) cause allocation (`Cow::Owned`).

Binary parsing is also zero-copy for string values. In appinfo v41, root keys (app IDs) are owned due to integer-to-string conversion, but nested values remain borrowed from the string table or input buffer.

## License

Licensed under either of:

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.
