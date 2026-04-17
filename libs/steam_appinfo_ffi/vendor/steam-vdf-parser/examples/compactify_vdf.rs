//! Compactify an appinfo.vdf file by keeping only the first N apps.
//!
//! Usage:
//!     cargo run --example compactify_vdf <input.vdf> <output.vdf> [--count N]
//!
//! Default count is 5.

use std::env;
use std::fs;
use std::path::Path;

use steam_vdf_parser::binary::{
    APPINFO_MAGIC_40, APPINFO_MAGIC_41, read_u32_le_at, read_u64_le_at,
};

// App entry header size
const APPINFO_ENTRY_HEADER_SIZE: usize = 68;

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 3 {
        eprintln!("Usage: {} <input.vdf> <output.vdf> [--count N]", args[0]);
        eprintln!("  --count N: Keep only the first N apps (default: 5)");
        std::process::exit(1);
    }

    let input_path = Path::new(&args[1]);
    let output_path = Path::new(&args[2]);

    // Parse count argument
    let mut count: usize = 5;
    if args.len() >= 4 {
        if args[3] == "--count" {
            if args.len() < 5 {
                eprintln!("Error: --count requires a number");
                std::process::exit(1);
            }
            count = match args[4].parse::<usize>() {
                Ok(n) if n > 0 => n,
                _ => {
                    eprintln!("Error: count must be a positive integer");
                    std::process::exit(1);
                }
            };
        } else {
            eprintln!("Error: unknown argument {}", args[3]);
            std::process::exit(1);
        }
    }

    // Read input file
    let data = match fs::read(input_path) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("Error reading input file: {}", e);
            std::process::exit(1);
        }
    };

    // Parse header
    if data.len() < 16 {
        eprintln!("Error: file too small to be a valid appinfo.vdf");
        std::process::exit(1);
    }

    let magic = match read_u32_le_at(&data, 0) {
        Some(m) => m,
        None => {
            eprintln!("Error: cannot read magic number");
            std::process::exit(1);
        }
    };

    let universe = match read_u32_le_at(&data, 4) {
        Some(u) => u,
        None => {
            eprintln!("Error: cannot read universe");
            std::process::exit(1);
        }
    };

    let (is_v41, string_table_offset) = match magic {
        APPINFO_MAGIC_40 => (false, None),
        APPINFO_MAGIC_41 => {
            let offset = read_u64_le_at(&data, 8);
            (true, offset.map(|o| o as usize))
        }
        _ => {
            eprintln!(
                "Error: invalid magic number {:08x}, expected appinfo.vdf format",
                magic
            );
            std::process::exit(1);
        }
    };

    if is_v41 && string_table_offset.is_none() {
        eprintln!("Error: cannot read string table offset for v41 format");
        std::process::exit(1);
    }

    println!(
        "Detected appinfo.vdf version: {}",
        if is_v41 { 41 } else { 40 }
    );
    println!("Universe: {}", universe);
    if let Some(offset) = string_table_offset {
        println!("String table offset: {}", offset);
    }

    // Find app entries to keep
    // App entries start at offset 16 (after magic + universe + optional string table offset)
    const HEADER_SIZE: usize = 16;

    let mut apps_end = data.len();
    if let Some(offset) = string_table_offset {
        apps_end = offset;
    }

    let mut current_offset = HEADER_SIZE;
    let mut selected_apps: Vec<(usize, usize)> = Vec::new(); // (start, size) for each app

    for _ in 0..count {
        if current_offset >= apps_end {
            break;
        }

        // Check we have enough data for the entry header
        if current_offset + APPINFO_ENTRY_HEADER_SIZE > data.len() {
            eprintln!("Warning: incomplete app entry at offset {}", current_offset);
            break;
        }

        // Read app ID
        let app_id = match read_u32_le_at(&data, current_offset) {
            Some(id) => id,
            None => {
                eprintln!("Error: cannot read app_id at offset {}", current_offset);
                std::process::exit(1);
            }
        };

        // Check for terminator
        if app_id == 0 {
            println!(
                "Reached terminator (app_id == 0) at offset {}",
                current_offset
            );
            break;
        }

        // Read size field (at offset 4)
        let entry_size = match read_u32_le_at(&data, current_offset + 4) {
            Some(s) => s as usize,
            None => {
                eprintln!("Error: cannot read size at offset {}", current_offset + 4);
                std::process::exit(1);
            }
        };

        // Total size of this app entry = header (8) + size field value
        // The size field includes APPINFO_HEADER_AFTER_SIZE (60) + VDF data
        let total_entry_size = 8 + entry_size;

        // Verify we have enough data
        if current_offset + total_entry_size > apps_end {
            eprintln!(
                "Warning: app entry extends past string table/EOF at offset {}",
                current_offset
            );
            break;
        }

        println!(
            "Selecting app_id {} at offset {}, size {}",
            app_id, current_offset, total_entry_size
        );

        selected_apps.push((current_offset, total_entry_size));
        current_offset += total_entry_size;
    }

    if selected_apps.is_empty() {
        eprintln!("Error: no app entries found in file");
        std::process::exit(1);
    }

    println!("Selected {} app entries", selected_apps.len());

    // Build output file
    let mut output = Vec::new();

    // Write header
    output.extend_from_slice(&data[0..HEADER_SIZE]);

    // Update string table offset for v41 (will be calculated later)
    let string_table_offset_placeholder = if is_v41 { Some(output.len() - 8) } else { None };

    // Write selected app entries
    for (offset, size) in &selected_apps {
        output.extend_from_slice(&data[*offset..*offset + *size]);
    }

    // For v41: copy string table
    // For v40: add terminator (4 bytes of 0x00)
    if is_v41 {
        let string_table_offset = string_table_offset.unwrap();
        let string_table_data = &data[string_table_offset..];

        // Update string table offset in header
        let new_offset = output.len() as u64;
        let offset_bytes = new_offset.to_le_bytes();
        if let Some(pos) = string_table_offset_placeholder {
            output[pos..pos + 8].copy_from_slice(&offset_bytes);
        }

        println!(
            "String table at original offset {}, new offset {}",
            string_table_offset, new_offset
        );

        // Copy string table
        output.extend_from_slice(string_table_data);
    } else {
        // v40: add terminator
        output.extend_from_slice(&[0x00, 0x00, 0x00, 0x00]);
    }

    // Write output file
    if let Err(e) = fs::write(output_path, &output) {
        eprintln!("Error writing output file: {}", e);
        std::process::exit(1);
    }

    println!(
        "Wrote {} bytes (original: {} bytes, reduction: {}%)",
        output.len(),
        data.len(),
        (data.len() - output.len()) * 100 / data.len()
    );
}
