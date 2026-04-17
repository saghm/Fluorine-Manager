//! Verify that a compactified appinfo.vdf file parses correctly.

use std::env;
use steam_vdf_parser::binary::parse_appinfo;

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        eprintln!("Usage: {} <vdf_file>", args[0]);
        std::process::exit(1);
    }

    let path = &args[1];

    // Read the file
    let data = match std::fs::read(path) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("Error reading file: {}", e);
            std::process::exit(1);
        }
    };

    // Parse it
    match parse_appinfo(&data) {
        Ok(vdf) => {
            println!("Successfully parsed {}", path);
            println!("Root key: {}", vdf.key());
            if let Some(obj) = vdf.as_obj() {
                println!("Number of apps: {}", obj.len());
                for (key, _) in obj.iter().take(5) {
                    println!("  - app_id: {}", key);
                }
                if obj.len() > 5 {
                    println!("  ... and {} more", obj.len() - 5);
                }
            }
        }
        Err(e) => {
            eprintln!("Error parsing file: {:?}", e);
            std::process::exit(1);
        }
    }
}
