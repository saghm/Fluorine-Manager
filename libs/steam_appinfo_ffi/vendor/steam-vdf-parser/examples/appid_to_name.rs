// Example: Extract AppId to game name mapping from Steam's appinfo.vdf
//
// Usage:
//   cargo run --example appid_to_name -- path/to/appinfo.vdf
//
// The appinfo.vdf file is typically located at:
//   - Windows: C:\Program Files (x86)\Steam\appcache\appinfo.vdf
//   - Linux: ~/.steam/steam/appcache/appinfo.vdf
//   - macOS: ~/Library/Application Support/Steam/appcache/appinfo.vdf

use std::env;
use std::fs;
use std::process::ExitCode;

use steam_vdf_parser::parse_appinfo;

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        eprintln!("Usage: {} <path/to/appinfo.vdf>", args[0]);
        eprintln!();
        eprintln!("Extracts AppId to game name mappings from Steam's appinfo.vdf file.");
        eprintln!();
        eprintln!("The appinfo.vdf file is typically located at:");
        eprintln!("  Windows: C:\\Program Files (x86)\\Steam\\appcache\\appinfo.vdf");
        eprintln!("  Linux:   ~/.steam/steam/appcache/appinfo.vdf");
        eprintln!("  macOS:   ~/Library/Application Support/Steam/appcache/appinfo.vdf");
        return ExitCode::FAILURE;
    }

    let path = &args[1];

    // Read the file
    let data = match fs::read(path) {
        Ok(data) => data,
        Err(e) => {
            eprintln!("Error reading file '{}': {}", path, e);
            return ExitCode::FAILURE;
        }
    };

    // Parse the appinfo.vdf file
    let vdf = match parse_appinfo(&data) {
        Ok(vdf) => vdf.into_owned(),
        Err(e) => {
            eprintln!("Error parsing appinfo.vdf: {}", e);
            return ExitCode::FAILURE;
        }
    };

    // Get the root object containing all apps
    let root = match vdf.as_obj() {
        Some(obj) => obj,
        None => {
            eprintln!("Error: root is not an object");
            return ExitCode::FAILURE;
        }
    };

    // Iterate through all apps (keyed by AppID as string)
    let mut apps = Vec::new();

    for (app_id_str, app_value) in root.iter() {
        // Skip non-numeric keys (metadata entries)
        if app_id_str.parse::<u32>().is_err() {
            continue;
        }

        let app_obj = match app_value.as_obj() {
            Some(obj) => obj,
            None => continue,
        };

        // Navigate the nested structure: appinfo -> common -> name
        let name = app_obj
            .get("appinfo")
            .and_then(|v| v.as_obj())
            .and_then(|appinfo| appinfo.get("common"))
            .and_then(|common| common.as_obj())
            .and_then(|common| common.get("name"))
            .and_then(|v| v.as_str());

        if let (Some(name), Ok(app_id)) = (name, app_id_str.parse::<u32>()) {
            apps.push((app_id, name.to_string()));
        }
    }

    // Sort by AppID
    apps.sort_by_key(|(id, _)| *id);

    // Print the results
    println!("AppId\tName");
    println!("------\t{}", "-".repeat(80));
    for (app_id, name) in &apps {
        println!("{}\t{}", app_id, name);
    }

    println!();
    println!("Total games: {}", apps.len());

    ExitCode::SUCCESS
}
