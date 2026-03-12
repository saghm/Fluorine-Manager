//! Extract icons from Windows PE (.exe/.dll) files using pelite.
//!
//! Returns raw ICO file bytes that can be loaded directly by Qt's QIcon/QImage.

use std::path::Path;

use pelite::resources::{Entry, Name, Resources};

/// Extract the best (largest) icon from a PE executable as raw ICO bytes.
///
/// Returns `None` if the file can't be read, isn't a valid PE, or has no icons.
pub fn extract_icon(exe_path: &Path) -> Option<Vec<u8>> {
    let data = std::fs::read(exe_path).ok()?;

    // Try PE32+ (64-bit) first, fall back to PE32 (32-bit)
    if let Ok(icon) = extract_from_pe64(&data) {
        return Some(icon);
    }
    if let Ok(icon) = extract_from_pe32(&data) {
        return Some(icon);
    }

    None
}

fn extract_from_pe64(data: &[u8]) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    use pelite::pe64::{Pe, PeFile};
    let pe = PeFile::from_bytes(data)?;
    let resources = pe.resources()?;
    build_ico_from_resources(&resources)
}

fn extract_from_pe32(data: &[u8]) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    use pelite::pe32::{Pe, PeFile};
    let pe = PeFile::from_bytes(data)?;
    let resources = pe.resources()?;
    build_ico_from_resources(&resources)
}

/// Build an ICO file from PE resources, picking the group icon with the
/// largest total pixel area.
fn build_ico_from_resources(
    resources: &Resources,
) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let root = resources.root()?;

    // Find RT_GROUP_ICON directory (type 14)
    let group_icon_dir = root
        .get_dir(Name::Id(14))
        .map_err(|_| "no RT_GROUP_ICON")?;

    let mut best_group: Option<(i32, Vec<u8>)> = None;

    for dir_entry in group_icon_dir.entries() {
        // Each entry is a group; get its subdirectory (language variants)
        let sub_dir = match dir_entry.entry() {
            Ok(Entry::Directory(d)) => d,
            _ => continue,
        };

        // Get the first language variant's data
        let data_entry = match sub_dir.entries().next() {
            Some(e) => match e.entry() {
                Ok(Entry::DataEntry(d)) => d,
                _ => continue,
            },
            None => continue,
        };

        let grp_data = match data_entry.bytes() {
            Ok(d) => d,
            Err(_) => continue,
        };

        // Parse GRPICONDIR header: reserved(2) + type(2) + count(2) = 6 bytes
        if grp_data.len() < 6 {
            continue;
        }
        let count = u16::from_le_bytes([grp_data[4], grp_data[5]]) as usize;
        // Each GRPICONDIRENTRY is 14 bytes
        if grp_data.len() < 6 + count * 14 {
            continue;
        }

        // Calculate total pixel area for ranking
        let mut total_area: i32 = 0;
        for i in 0..count {
            let offset = 6 + i * 14;
            let w = if grp_data[offset] == 0 {
                256i32
            } else {
                grp_data[offset] as i32
            };
            let h = if grp_data[offset + 1] == 0 {
                256i32
            } else {
                grp_data[offset + 1] as i32
            };
            total_area += w * h;
        }

        if let Ok(ico_bytes) = build_ico_file(resources, grp_data, count) {
            if best_group.is_none() || total_area > best_group.as_ref().unwrap().0 {
                best_group = Some((total_area, ico_bytes));
            }
        }
    }

    best_group
        .map(|(_, bytes)| bytes)
        .ok_or_else(|| "no valid icon group found".into())
}

/// Build a complete ICO file from a GRPICONDIR and the corresponding RT_ICON resources.
fn build_ico_file(
    resources: &Resources,
    grp_data: &[u8],
    count: usize,
) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let root = resources.root()?;

    // RT_ICON = type 3
    let icon_dir = root.get_dir(Name::Id(3)).map_err(|_| "no RT_ICON")?;

    // Collect individual icon image data
    struct IconEntry<'a> {
        header: [u8; 8], // first 8 bytes of GRPICONDIRENTRY (w, h, colors, reserved, planes, bpp)
        data: &'a [u8],
    }
    let mut entries: Vec<IconEntry> = Vec::new();

    for i in 0..count {
        let grp_offset = 6 + i * 14;
        let icon_id =
            u16::from_le_bytes([grp_data[grp_offset + 12], grp_data[grp_offset + 13]]) as u32;

        // Find the RT_ICON with this ID
        let icon_sub = match icon_dir.get_dir(Name::Id(icon_id)) {
            Ok(d) => d,
            Err(_) => continue,
        };

        let data_entry = match icon_sub.entries().next() {
            Some(e) => match e.entry() {
                Ok(Entry::DataEntry(d)) => d,
                _ => continue,
            },
            None => continue,
        };

        let image_data = match data_entry.bytes() {
            Ok(d) => d,
            Err(_) => continue,
        };

        let mut header = [0u8; 8];
        header.copy_from_slice(&grp_data[grp_offset..grp_offset + 8]);

        entries.push(IconEntry {
            header,
            data: image_data,
        });
    }

    if entries.is_empty() {
        return Err("no icon entries found".into());
    }

    let entry_count = entries.len() as u16;
    let header_size = 6 + (entry_count as usize) * 16; // ICONDIR + ICONDIRENTRYs

    let mut ico = Vec::new();

    // ICONDIR header
    ico.extend_from_slice(&0u16.to_le_bytes()); // reserved
    ico.extend_from_slice(&1u16.to_le_bytes()); // type = ICO
    ico.extend_from_slice(&entry_count.to_le_bytes());

    // Write ICONDIRENTRY records (16 bytes each)
    let mut data_offset = header_size as u32;
    for entry in &entries {
        ico.extend_from_slice(&entry.header); // width, height, colors, reserved, planes, bpp (8 bytes)
        let size = entry.data.len() as u32;
        ico.extend_from_slice(&size.to_le_bytes()); // dwBytesInRes (4 bytes)
        ico.extend_from_slice(&data_offset.to_le_bytes()); // dwImageOffset (4 bytes)
        data_offset += size;
    }

    // Write image data
    for entry in &entries {
        ico.extend_from_slice(entry.data);
    }

    Ok(ico)
}
