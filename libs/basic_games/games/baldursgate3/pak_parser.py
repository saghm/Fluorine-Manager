from __future__ import annotations

import configparser
import hashlib
import os
import platform
import re
import shutil
import subprocess
import traceback
from functools import cached_property
from pathlib import Path
from typing import Callable
from xml.etree import ElementTree
from xml.etree.ElementTree import Element

import larian_formats
from PyQt6.QtCore import (
    qDebug,
    qInfo,
    qWarning,
)

import mobase

from . import bg3_utils


class BG3PakParser:
    def __init__(self, utils: bg3_utils.BG3Utils):
        self._utils = utils

    _mod_cache: dict[Path, bool] = {}
    _types = {
        "Folder": "",
        "MD5": "",
        "Name": "",
        "PublishHandle": "0",
        "UUID": "",
        "Version64": "0",
    }

    @cached_property
    def _folder_pattern(self):
        return re.compile("Data|Script Extender|bin|Mods")

    def get_metadata_for_files_in_mod(
        self, mod: mobase.IModInterface, force_reparse_metadata: bool
    ):
        return {
            mod.name(): "".join(
                [
                    self._get_metadata_for_file(mod, file, force_reparse_metadata)
                    for file in sorted(
                        list(Path(mod.absolutePath()).rglob("*.pak"))
                        + (
                            [
                                f
                                for f in Path(mod.absolutePath()).glob("*")
                                if f.is_dir()
                            ]
                            if self._utils.autobuild_paks
                            else []
                        )
                    )
                ]
            )
        }

    def _get_metadata_for_file(
        self,
        mod: mobase.IModInterface,
        file: Path,
        force_reparse_metadata: bool,
    ) -> str:
        meta_ini = Path(mod.absolutePath()) / "meta.ini"
        config = configparser.ConfigParser(interpolation=None)
        config.read(meta_ini, encoding="utf-8")
        try:
            if file.name.endswith("pak"):
                if (
                    not force_reparse_metadata
                    and config.has_section(file.name)
                    and (
                        "override" in config[file.name].keys()
                        or "Folder" in config[file.name].keys()
                    )
                ):
                    return get_module_short_desc(config, file)

                return self.metadata_to_ini(config, file, mod, meta_ini)
            elif file.is_dir():
                if self._folder_pattern.search(file.name):
                    return ""
                for folder in bg3_utils.loose_file_folders:
                    if next(file.glob(f"{folder}/*"), False):
                        break
                else:
                    return ""
                qInfo(f"packable dir: {file}")
                if (file.parent / f"{file.name}.pak").exists() or (
                    file.parent / "Mods" / f"{file.name}.pak"
                ).exists():
                    qInfo(
                        f"pak with same name as packable dir exists in mod directory. not packing dir {file}"
                    )
                    return ""
                parent_mod_name = file.parent.name.replace(" ", "_")
                pak_path = (
                    self._utils.overwrite_path
                    / f"Mods/{parent_mod_name}_{file.name}.pak"
                )
                build_pak = True
                if pak_path.exists():
                    try:
                        pak_creation_time = os.path.getmtime(pak_path)
                        for root, _, files in file.walk():
                            for f in files:
                                file_path = root.joinpath(f)
                                try:
                                    if os.path.getmtime(file_path) > pak_creation_time:
                                        break
                                except OSError as e:
                                    qDebug(f"Error accessing file {file_path}: {e}")
                                    break
                        else:
                            build_pak = False
                    except OSError as e:
                        qDebug(f"Error accessing file {pak_path}: {e}")
                        build_pak = False
                if build_pak:
                    pak_path.unlink(missing_ok=True)

                    larian_formats.pack_loose_files(file.parent, pak_path)
                    output = ""

                    try:
                        output = self.metadata_to_ini(
                            config,
                            pak_path,
                            mod,
                            meta_ini,
                        )
                    except:
                        pass

                return output
            else:
                return ""
        except Exception:
            qWarning(traceback.format_exc())
            return ""

    def get_attr_value(self, root: Element, attr_id: str) -> str:
        default_val = self._types.get(attr_id) or ""
        attr = root.find(f".//attribute[@id='{attr_id}']")
        return default_val if attr is None else attr.get("value", default_val)

    def metadata_to_ini(
        self,
        config: configparser.ConfigParser,
        file: Path,
        mod: mobase.IModInterface,
        meta_ini: Path,
    ):
        config[file.name] = {}
        metadata = larian_formats.get_metadata_for_file(file)
        config[file.name].update({k: str(v) for k, v in metadata.items()})

        if larian_formats.is_override(file):
            config[file.name]["override"] = "True"
        with open(meta_ini, "w+", encoding="utf-8") as f:
            config.write(f)
        return get_module_short_desc(config, file)


def get_module_short_desc(config: configparser.ConfigParser, file: Path) -> str:
    if not config.has_section(file.name):
        return ""
    section: configparser.SectionProxy = config[file.name]
    return (
        ""
        if "override" in section.keys() or "Name" not in section.keys()
        else bg3_utils.get_node_string(
            folder=section["Folder"],
            md5=section["MD5"],
            name=section["Name"],
            publish_handle=section["PublishHandle"],
            uuid=section["UUID"],
            version64=section["Version64"],
        )
    )
