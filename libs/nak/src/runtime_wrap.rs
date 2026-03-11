use std::ffi::OsStr;
use std::process::Command;

/// Build a command to run `exe` with the given environment variables.
pub fn build_command<S: AsRef<OsStr>>(
    exe: impl AsRef<OsStr>,
    envs: &[(&str, S)],
) -> Command {
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
