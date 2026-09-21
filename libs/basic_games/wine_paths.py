"""Resolve Wine user paths without depending on Qt or the game plugin loader."""

from __future__ import annotations

import json
import os
import platform
from pathlib import Path


def _user_profile(prefix: str) -> str | None:
    if not prefix.strip():
        return None
    root = Path(prefix.strip())
    if not (root / "drive_c").is_dir() and (root / "pfx/drive_c").is_dir():
        root /= "pfx"
    user = root / "drive_c/users/steamuser"
    return str(user) if user.is_dir() else None


def find_wine_userprofile(organizer=None) -> str | None:
    """Use the launcher's prefix once initialized; fall back only for older hosts.

    An empty/missing active prefix must not silently select another installation.
    Before initialization (or on older hosts), prefer the configured Fluorine
    prefix over the historical default location.
    """
    if platform.system() == "Windows":
        return None
    resolver = getattr(organizer, "winePrefixPath", None)
    if callable(resolver):
        return _user_profile(resolver())

    config_root = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    try:
        config = json.loads((config_root / "fluorine/config.json").read_text())
        prefix = config.get("prefix_path", "") if isinstance(config, dict) else ""
        if isinstance(prefix, str) and (user := _user_profile(prefix)):
            return user
    except (OSError, ValueError):
        pass
    data_root = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    if not data_root.is_absolute():
        data_root = Path.home() / ".local/share"
    return _user_profile(str(data_root / "fluorine/Prefix/pfx"))
