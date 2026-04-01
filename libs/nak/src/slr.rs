//! Steam Linux Runtime (SLR) download and management for Fluorine Manager.
//!
//! Downloads SteamLinuxRuntime_sniper from Valve's official repo and stores it
//! at `~/.local/share/fluorine/steamrt/`.  The `run` script is then used to
//! wrap game launches inside the pressure-vessel container, providing
//! GStreamer, 32-bit libs, and an FHS-compliant environment for non-FHS
//! distros (NixOS, etc.).

use std::error::Error;
use std::fs;
use std::io::{Read, Write};
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicI32, Ordering};

use crate::logging::log_info;

const BASE_URL: &str =
    "https://repo.steampowered.com/steamrt3/images/latest-public-beta";
const ARCHIVE_NAME: &str = "SteamLinuxRuntime_sniper.tar.xz";
const EXTRACTED_DIR: &str = "SteamLinuxRuntime_sniper";

/// Directory where SLR is installed: `~/.local/share/fluorine/steamrt/`
pub fn slr_install_dir() -> PathBuf {
    crate::paths::data_dir().join("steamrt")
}

/// Path to the `run` script inside the extracted SLR.
pub fn slr_run_script() -> PathBuf {
    slr_install_dir().join(EXTRACTED_DIR).join("run")
}

/// Path where we store the remote BUILD_ID for update checks.
fn local_build_id_path() -> PathBuf {
    slr_install_dir().join("BUILD_ID.txt")
}

/// Returns true if the SLR `run` script is present and executable.
pub fn is_slr_installed() -> bool {
    let script = slr_run_script();
    if !script.exists() {
        return false;
    }
    // Verify it's actually executable
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if let Ok(meta) = fs::metadata(&script) {
            return meta.permissions().mode() & 0o111 != 0;
        }
        return false;
    }
    #[cfg(not(unix))]
    true
}

/// Returns the path to the `run` script, or None if SLR is not installed.
pub fn get_slr_run_script() -> Option<PathBuf> {
    if is_slr_installed() {
        Some(slr_run_script())
    } else {
        None
    }
}

/// Fetch the remote BUILD_ID as a string.
fn fetch_remote_build_id() -> Result<String, Box<dyn Error>> {
    let url = format!("{}/BUILD_ID.txt", BASE_URL);
    let resp = ureq::get(&url).call()?;
    let mut body = String::new();
    resp.into_reader().read_to_string(&mut body)?;
    Ok(body.trim().to_string())
}

/// Read the locally cached BUILD_ID, if any.
fn read_local_build_id() -> Option<String> {
    fs::read_to_string(local_build_id_path())
        .ok()
        .map(|s| s.trim().to_string())
}

/// Verify downloaded file size matches the Content-Length from the HTTP response.
/// This is more reliable than SHA256SUMS which can be stale on Valve's CDN.
fn verify_download_size(file: &std::path::Path, expected: u64) -> Result<(), Box<dyn Error>> {
    let actual = fs::metadata(file)?.len();
    if actual != expected {
        return Err(format!(
            "Download incomplete: expected {} bytes, got {}",
            expected, actual
        )
        .into());
    }
    Ok(())
}

/// Download the archive with streaming progress.
///
/// `progress_cb` receives values in 0.0..=1.0.
/// `cancel_flag` is polled each chunk — set to non-zero to abort.
/// Returns the Content-Length on success for verification.
fn download_archive(
    dest: &std::path::Path,
    progress_cb: &impl Fn(f32),
    cancel_flag: &AtomicI32,
) -> Result<Option<u64>, Box<dyn Error>> {
    let url = format!("{}/{}", BASE_URL, ARCHIVE_NAME);
    let resp = ureq::get(&url).call()?;

    let total_bytes: Option<u64> = resp
        .header("Content-Length")
        .and_then(|v| v.parse().ok());

    let mut reader = resp.into_reader();
    let mut file = fs::File::create(dest)?;
    let mut buf = vec![0u8; 64 * 1024]; // 64 KiB chunks
    let mut downloaded: u64 = 0;

    loop {
        if cancel_flag.load(Ordering::Relaxed) != 0 {
            drop(file);
            let _ = fs::remove_file(dest);
            return Err("Download cancelled".into());
        }

        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        file.write_all(&buf[..n])?;
        downloaded += n as u64;

        if let Some(total) = total_bytes {
            if total > 0 {
                progress_cb((downloaded as f32) / (total as f32));
            }
        }
    }

    file.flush()?;
    Ok(total_bytes)
}

/// Download and install the Steam Linux Runtime (sniper).
///
/// - Skips download if already at the latest BUILD_ID.
/// - Calls `status_cb` with human-readable status strings.
/// - Calls `progress_cb` with 0.0..=1.0 during the download phase.
/// - Polls `cancel_flag`; returns an error if it becomes non-zero.
pub fn download_slr(
    progress_cb: impl Fn(f32),
    status_cb: impl Fn(&str),
    cancel_flag: &AtomicI32,
) -> Result<(), Box<dyn Error>> {
    // Check for updates
    status_cb("Checking Steam Linux Runtime version...");
    let remote_build_id = fetch_remote_build_id()?;
    let local_build_id = read_local_build_id();

    if local_build_id.as_deref() == Some(remote_build_id.as_str()) && is_slr_installed() {
        log_info("Steam Linux Runtime is already up to date");
        status_cb("Steam Linux Runtime is already up to date");
        progress_cb(1.0);
        return Ok(());
    }

    log_info(&format!(
        "Downloading Steam Linux Runtime (BUILD_ID: {})",
        remote_build_id
    ));

    let install_dir = slr_install_dir();
    fs::create_dir_all(&install_dir)?;

    // Temp file in the install dir
    let archive_path = install_dir.join(ARCHIVE_NAME);

    // Download
    status_cb("Downloading Steam Linux Runtime (sniper, ~180 MB)...");
    let content_length = download_archive(&archive_path, &progress_cb, cancel_flag)?;
    progress_cb(1.0);

    // Verify download integrity via Content-Length.
    // We don't use Valve's SHA256SUMS file because their CDN frequently
    // serves a stale copy that doesn't match the current archive.
    if let Some(expected_size) = content_length {
        status_cb("Verifying download...");
        if let Err(e) = verify_download_size(&archive_path, expected_size) {
            let _ = fs::remove_file(&archive_path);
            return Err(format!("Download verification failed: {}", e).into());
        }
        log_info(&format!(
            "Download verified ({} bytes)",
            expected_size
        ));
    }

    // Extract
    status_cb("Extracting Steam Linux Runtime...");
    let extracted = install_dir.join(EXTRACTED_DIR);
    // Remove old copy if present before extracting
    if extracted.exists() {
        fs::remove_dir_all(&extracted)?;
    }

    let status = Command::new("tar")
        .args(["xJf", archive_path.to_str().unwrap_or("")])
        .current_dir(&install_dir)
        .status()?;

    let _ = fs::remove_file(&archive_path);

    if !status.success() {
        return Err(format!("tar extraction failed with status: {}", status).into());
    }

    // Sanity check — run script must now exist
    if !slr_run_script().exists() {
        return Err(format!(
            "Extraction succeeded but run script not found at {:?}",
            slr_run_script()
        )
        .into());
    }

    // Save BUILD_ID for future update checks
    fs::write(local_build_id_path(), &remote_build_id)?;

    log_info("Steam Linux Runtime installed successfully");
    status_cb("Steam Linux Runtime ready");
    Ok(())
}
