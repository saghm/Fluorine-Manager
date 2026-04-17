//! Pretty-print a VDF file using the `{:#}` Display format.
//!
//! Usage: cargo run --example pretty_print <vdf_file>
//!
//! This example demonstrates the pretty-print Display implementation that
//! outputs valid VDF text format with proper indentation and escaping.

use std::env;
use std::path::Path;
use steam_vdf_parser::{binary, parse_binary, parse_text};

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <vdf_file>", args[0]);
        eprintln!();
        eprintln!("Pretty-prints a VDF file to stdout in valid VDF text format.");
        eprintln!(
            "Supports both text (.vdf) and binary (appinfo.vdf, packageinfo.vdf, shortcuts.vdf) formats."
        );
        std::process::exit(1);
    }

    let path = Path::new(&args[1]);
    let data = std::fs::read(path).expect("Failed to read file");

    // Try to detect format and parse accordingly
    let result = if let Ok(text) = std::str::from_utf8(&data) {
        // Looks like text, try text parser first
        parse_text(text)
            .map(|v| v.into_owned())
            .or_else(|_| parse_binary(&data).map(|v| v.into_owned()))
    } else {
        // Binary data - detect format from filename
        let filename = path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or_default();

        if filename.contains("packageinfo") {
            binary::parse_packageinfo(&data).map(|v| v.into_owned())
        } else if filename.contains("appinfo") {
            binary::parse_appinfo(&data).map(|v| v.into_owned())
        } else {
            parse_binary(&data).map(|v| v.into_owned())
        }
    };

    match result {
        Ok(vdf) => {
            // Use the pretty-print Display implementation
            println!("{:#}", vdf);
        }
        Err(e) => {
            eprintln!("Error parsing VDF: {:?}", e);
            std::process::exit(1);
        }
    }
}
