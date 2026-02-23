use std::env;
use std::ffi::OsStr;
use std::process::Command;

fn env_flag(name: &str) -> bool {
    matches!(
        env::var(name)
            .unwrap_or_default()
            .trim()
            .to_ascii_lowercase()
            .as_str(),
        "1" | "true" | "yes" | "on"
    )
}

pub fn use_steam_run() -> bool {
    env_flag("NAK_USE_STEAM_RUN")
}

/// Build a command to run `exe` with the given environment variables.
///
/// In steam-run mode, the command is wrapped with `steam-run`.
/// Otherwise the command runs directly.
pub fn build_command<S: AsRef<OsStr>>(
    exe: impl AsRef<OsStr>,
    envs: &[(&str, S)],
) -> Command {
    if use_steam_run() {
        let mut cmd = Command::new("steam-run");
        cmd.arg(exe.as_ref());
        for (key, value) in envs {
            cmd.env(key, value.as_ref());
        }
        return cmd;
    }
    let mut cmd = Command::new(exe);
    for (key, value) in envs {
        cmd.env(key, value.as_ref());
    }
    cmd
}

/// Build a command with no extra environment variables.
pub fn command_for(exe: impl AsRef<OsStr>) -> Command {
    build_command::<&str>(exe, &[])
}
