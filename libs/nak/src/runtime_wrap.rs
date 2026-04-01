use std::ffi::OsStr;
use std::process::Command;

/// Build a command to run `exe` with the given environment variables.
///
/// If SLR (Steam Linux Runtime) is installed, the command is wrapped inside
/// the pressure-vessel container via the SLR `run` script.  Environment
/// variables are set on the process and inherited by pressure-vessel into the
/// container (matching how game launches work via QProcess).  This ensures
/// Wine/Proton commands use the container's libraries instead of potentially
/// broken host libraries.
pub fn build_command<S: AsRef<OsStr>>(
    exe: impl AsRef<OsStr>,
    envs: &[(&str, S)],
) -> Command {
    if let Some(slr_script) = crate::slr::get_slr_run_script() {
        let mut cmd = Command::new(&slr_script);
        // Set env vars on the process — pressure-vessel inherits them
        for (key, value) in envs {
            cmd.env(key, value.as_ref());
        }
        // Expose the executable's parent directory to the container —
        // needed for system-installed Protons (e.g. /usr/share/steam/...)
        // whose files may not be visible inside the container by default.
        if let Some(parent) = std::path::Path::new(exe.as_ref()).parent() {
            if parent.exists() {
                let mut flag = std::ffi::OsString::from("--filesystem=");
                flag.push(parent.as_os_str());
                cmd.arg(flag);
            }
        }
        // Also expose WINEPREFIX if set in env vars
        for (key, value) in envs {
            if *key == "WINEPREFIX" || *key == "STEAM_COMPAT_DATA_PATH" {
                let path = std::path::Path::new(value.as_ref());
                if path.exists() || path.parent().map_or(false, |p| p.exists()) {
                    let mut flag = std::ffi::OsString::from("--filesystem=");
                    flag.push(value.as_ref());
                    cmd.arg(flag);
                }
            }
        }
        cmd.arg("--");
        cmd.arg(exe);
        cmd
    } else {
        let mut cmd = Command::new(exe);
        for (key, value) in envs {
            cmd.env(key, value.as_ref());
        }
        cmd
    }
}

/// Build a command with no extra environment variables.
pub fn command_for(exe: impl AsRef<OsStr>) -> Command {
    build_command::<&str>(exe, &[])
}
