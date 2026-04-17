use std::env;
use std::path::Path;
use steam_vdf_parser::{binary, parse_binary, parse_text};

fn dump_value(value: &steam_vdf_parser::Value, indent: usize) -> String {
    let indent_str = "  ".repeat(indent);
    match value {
        steam_vdf_parser::Value::Str(s) => format!("{}\"{}\"", indent_str, s),
        steam_vdf_parser::Value::I32(n) => format!("{}{}", indent_str, n),
        steam_vdf_parser::Value::U64(n) => format!("{}{}", indent_str, n),
        steam_vdf_parser::Value::Float(n) => format!("{}{}", indent_str, n),
        steam_vdf_parser::Value::Pointer(n) => format!("{}(pointer: {})", indent_str, n),
        steam_vdf_parser::Value::Color(c) => format!(
            "{}(color: #{:02x}{:02x}{:02x}{:02x})",
            indent_str, c[0], c[1], c[2], c[3]
        ),
        steam_vdf_parser::Value::Obj(obj) => {
            let mut out = format!("{}{{\n", indent_str);
            for (k, v) in obj.iter() {
                out.push_str(&format!("{}\"\"{}\"\": ", indent_str, k));
                match v {
                    steam_vdf_parser::Value::Obj(_) => {
                        out.push_str(&dump_value(v, indent + 1));
                    }
                    steam_vdf_parser::Value::Str(s) => out.push_str(&format!("\"{}\"\n", s)),
                    steam_vdf_parser::Value::I32(n) => out.push_str(&format!("{}\n", n)),
                    steam_vdf_parser::Value::U64(n) => out.push_str(&format!("{}\n", n)),
                    steam_vdf_parser::Value::Float(n) => out.push_str(&format!("{}\n", n)),
                    steam_vdf_parser::Value::Pointer(n) => {
                        out.push_str(&format!("(pointer: {})\n", n))
                    }
                    steam_vdf_parser::Value::Color(c) => out.push_str(&format!(
                        "(color: #{:02x}{:02x}{:02x}{:02x})\n",
                        c[0], c[1], c[2], c[3]
                    )),
                }
            }
            out.push_str(&format!("{}}}\n", indent_str));
            out
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <vdf_file> [--text]", args[0]);
        eprintln!(
            "  --text: force text format parsing (default: auto-detect based on file extension)"
        );
        std::process::exit(1);
    }

    let path = Path::new(&args[1]);
    let force_text = args.len() > 2 && args[2] == "--text";

    let result = if force_text || path.extension().is_some_and(|e| e == "vdf") {
        // Try text first for .vdf files
        let content = std::fs::read_to_string(path);
        if let Ok(content) = content {
            parse_text(&content).map(|v| v.into_owned())
        } else {
            // Fall back to binary
            let data = std::fs::read(path).expect("Failed to read file");
            if path
                .file_name()
                .is_some_and(|n| n.to_str().is_some_and(|s| s.contains("packageinfo")))
            {
                binary::parse_packageinfo(&data).map(|v| v.into_owned())
            } else if path
                .file_name()
                .is_some_and(|n| n.to_str().is_some_and(|s| s.contains("appinfo")))
            {
                binary::parse_appinfo(&data).map(|v| v.into_owned())
            } else {
                parse_binary(&data).map(|v| v.into_owned())
            }
        }
    } else {
        // Binary parsing
        let data = std::fs::read(path).expect("Failed to read file");
        if path
            .file_name()
            .is_some_and(|n| n.to_str().is_some_and(|s| s.contains("packageinfo")))
        {
            binary::parse_packageinfo(&data).map(|v| v.into_owned())
        } else if path
            .file_name()
            .is_some_and(|n| n.to_str().is_some_and(|s| s.contains("appinfo")))
        {
            binary::parse_appinfo(&data).map(|v| v.into_owned())
        } else {
            parse_binary(&data).map(|v| v.into_owned())
        }
    };

    match result {
        Ok(vdf) => {
            println!("\"{}\" {}", vdf.key(), dump_value(vdf.value(), 0));
        }
        Err(e) => {
            eprintln!("Error parsing VDF: {:?}", e);
            std::process::exit(1);
        }
    }
}
