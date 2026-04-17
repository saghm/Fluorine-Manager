use std::fs;
use steam_vdf_parser::{parse_binary, parse_text};

fn main() {
    // Test localconfig.vdf (text format)
    println!("=== Parsing localconfig.vdf (text format) ===");
    let localconfig = fs::read_to_string(
        "/home/mexus/.local/share/Steam/userdata/127648749/config/localconfig.vdf",
    );
    match localconfig {
        Ok(content) => match parse_text(&content) {
            Ok(vdf) => {
                println!("Success!");
                println!("Root key: {}", vdf.key());
                let obj = vdf.as_obj().unwrap();
                println!("Root has {} keys", obj.len());
            }
            Err(e) => {
                println!("Parse error: {:?}", e);
            }
        },
        Err(e) => println!("Error reading: {}", e),
    }

    println!("\n=== Parsing appinfo.vdf (binary format) ===");
    let appinfo = fs::read("/home/mexus/.local/share/Steam/appcache/appinfo.vdf");
    match appinfo {
        Ok(data) => match parse_binary(&data) {
            Ok(vdf) => {
                println!("Success!");
                println!("Root key: {}", vdf.key());
                let obj = vdf.as_obj().unwrap();
                println!("Root has {} keys", obj.len());
            }
            Err(e) => {
                println!("Parse error: {:?}", e);
            }
        },
        Err(e) => println!("Error reading: {}", e),
    }
}
