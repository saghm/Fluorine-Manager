#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
APPIMAGE_DIR="$(dirname "$(readlink -f "${APPIMAGE:-$0}")")"

cleanup_stale_fuse_mounts() {
    local root="$1"
    [ -n "${root}" ] || return 0
    [ -r /proc/mounts ] || return 0

    local src mp fstype rest mount_path stat_out
    while IFS=' ' read -r src mp fstype rest; do
        # Decode escaped paths from /proc/mounts.
        mount_path="${mp//\\040/ }"
        mount_path="${mount_path//\\011/$'\t'}"
        mount_path="${mount_path//\\012/$'\n'}"

        case "${mount_path}" in
            "${root}"/*) ;;
            *) continue ;;
        esac

        case "${fstype}" in
            fuse*|fuse.*) ;;
            *) continue ;;
        esac

        stat_out="$(LC_ALL=C stat "${mount_path}" 2>&1 || true)"
        if printf "%s" "${stat_out}" | grep -Eq "Transport endpoint is not connected|Stale file handle|Input/output error"; then
            echo "[Fluorine] Recovering stale FUSE mount: ${mount_path}"
            fusermount3 -uz "${mount_path}" >/dev/null 2>&1 || \
            fusermount -uz "${mount_path}" >/dev/null 2>&1 || \
            umount -l "${mount_path}" >/dev/null 2>&1 || true
        fi
    done < /proc/mounts
}

cleanup_stale_fuse_mounts "${APPIMAGE_DIR}"

sync_dir_overwrite_files() {
    local name="$1"
    local src="${HERE}/usr/share/fluorine/${name}"
    local dst="${APPIMAGE_DIR}/${name}"

    [ -d "${src}" ] || return 0

    if [ ! -d "${dst}" ]; then
        echo "[Fluorine] First run: extracting ${name}/ next to AppImage..."
    else
        echo "[Fluorine] Updating ${name}/ bundled files..."
    fi
    mkdir -p "${dst}"
    # Merge bundled payload while skipping VCS metadata (.git, etc.).
    (
        cd "${src}"
        tar --exclude-vcs -cf - .
    ) | (
        cd "${dst}"
        tar -xf -
    )
}

# ── Extract/sync writable dirs ──
sync_dir_overwrite_files "plugins"
sync_dir_overwrite_files "dlls"
sync_dir_overwrite_files "python"

# Some existing Windows portable setups include plugins/plugin_python with
# .pyd/.dll payload. On Linux, the proxy will prefer this folder if present,
# which can hide the correct Linux mobase module. Overlay Linux runtime files
# into that folder to keep compatibility without deleting user content.
if [ -d "${APPIMAGE_DIR}/plugins/plugin_python" ]; then
    if find "${APPIMAGE_DIR}/plugins/plugin_python" -type f \( -name '*.pyd' -o -name 'python*.dll' \) | grep -q .; then
        echo "[Fluorine] Detected Windows plugin_python payload, overlaying Linux runtime files..."
        mkdir -p "${APPIMAGE_DIR}/plugins/plugin_python/libs" "${APPIMAGE_DIR}/plugins/plugin_python/dlls"
        [ -d "${APPIMAGE_DIR}/plugins/libs" ] && cp -a "${APPIMAGE_DIR}/plugins/libs/." "${APPIMAGE_DIR}/plugins/plugin_python/libs/"
        [ -d "${APPIMAGE_DIR}/plugins/dlls" ] && cp -a "${APPIMAGE_DIR}/plugins/dlls/." "${APPIMAGE_DIR}/plugins/plugin_python/dlls/"
        PYTHON_ROOT_CANDIDATE="${HERE}/usr/share/fluorine/python"
        [ -d "${PYTHON_ROOT_CANDIDATE}/lib" ] || PYTHON_ROOT_CANDIDATE="${APPIMAGE_DIR}/python"
        if [ -d "${PYTHON_ROOT_CANDIDATE}/lib" ]; then
            PYVER_DIR="$(find "${PYTHON_ROOT_CANDIDATE}/lib" -mindepth 1 -maxdepth 1 -type d -name 'python3.*' | head -n 1)"
            if [ -n "${PYVER_DIR}" ]; then
                SITE_DIR="${PYVER_DIR}/site-packages"
                [ -d "${SITE_DIR}/PyQt6" ] && cp -a "${SITE_DIR}/PyQt6" "${APPIMAGE_DIR}/plugins/plugin_python/libs/"
                [ -d "${SITE_DIR}/PyQt6_sip" ] && cp -a "${SITE_DIR}/PyQt6_sip" "${APPIMAGE_DIR}/plugins/plugin_python/libs/"
                [ -d "${SITE_DIR}/sip" ] && cp -a "${SITE_DIR}/sip" "${APPIMAGE_DIR}/plugins/plugin_python/libs/"
            fi
        fi
    fi
fi

# ── Environment ──
# Save original LD_LIBRARY_PATH so child processes (xdg-open, kde-open, etc.)
# can use host libraries instead of the bundled (potentially older) ones.
export FLUORINE_ORIG_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export PATH="${HERE}/usr/bin:${HERE}/usr/libexec:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/libexec:${LD_LIBRARY_PATH:-}"

# Qt plugins (read-only, inside AppImage)
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/usr/plugins/platforms"
if [ -x "${HERE}/usr/libexec/QtWebEngineProcess" ]; then
    export QTWEBENGINEPROCESS_PATH="${HERE}/usr/libexec/QtWebEngineProcess"
fi
if [ -d "${HERE}/usr/resources" ]; then
    export QTWEBENGINE_RESOURCES_PATH="${HERE}/usr/resources"
fi
if [ -d "${HERE}/usr/translations/qtwebengine_locales" ]; then
    export QTWEBENGINE_LOCALES_PATH="${HERE}/usr/translations/qtwebengine_locales"
fi

# Tell the app to use the writable dirs next to the AppImage.
# MO2_BASE_DIR overrides qApp->applicationDirPath() for plugin/dll discovery.
export MO2_BASE_DIR="${APPIMAGE_DIR}"
export MO2_PLUGINS_DIR="${APPIMAGE_DIR}/plugins"
export MO2_DLLS_DIR="${APPIMAGE_DIR}/dlls"
MO2_PYTHON_BUNDLED="${HERE}/usr/share/fluorine/python"
if [ -d "${MO2_PYTHON_BUNDLED}/lib" ]; then
    export MO2_PYTHON_DIR="${MO2_PYTHON_BUNDLED}"
else
    export MO2_PYTHON_DIR="${APPIMAGE_DIR}/python"
fi
# Do not export PYTHONHOME/PYTHONPATH globally here. MO2 sets Python runtime
# internally for plugin_python, while child processes (NaK/launchers) must
# use their own system Python environment.
unset PYTHONHOME PYTHONPATH PYTHONNOUSERSITE

cd "${APPIMAGE_DIR}"
exec "${HERE}/usr/bin/ModOrganizer.bin" "$@"
