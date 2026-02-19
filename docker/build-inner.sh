#!/usr/bin/env bash
set -euo pipefail

BUILD_PY="${BUILD_PYTHON:-$(command -v python3)}"

# ── Build ──
PYBIND11_DIR="$("${BUILD_PY}" -c 'import pybind11; print(pybind11.get_cmake_dir())' 2>/dev/null || true)"

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPython_EXECUTABLE="${BUILD_PY}" \
    ${PYBIND11_DIR:+-Dpybind11_DIR="${PYBIND11_DIR}"} \
    -DBUILD_PLUGIN_PYTHON=ON

cmake --build build --parallel

MODORG_BIN="build/src/src/ModOrganizer"
if [ ! -f "${MODORG_BIN}" ]; then
    echo "ERROR: ModOrganizer binary not found at ${MODORG_BIN}"
    exit 1
fi
RUNDIR="build/src/src"

PY_MM="$("${BUILD_PY}" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"

# ── Output layout (staging area — installed to ~/.local/share/fluorine by build-native.sh) ──
OUT_DIR="/src/build/staging"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/plugins" "${OUT_DIR}/dlls" "${OUT_DIR}/lib"

# ── Main binary + helpers ──
cp -f "${RUNDIR}/ModOrganizer" "${OUT_DIR}/ModOrganizer-core"
if [ -f "${RUNDIR}/umu-run" ]; then
    # Patch umu-run to preserve STEAM_COMPAT_CLIENT_INSTALL_PATH from the
    # parent environment.  Upstream umu-run initialises this to "" and never
    # picks up the caller's value, which prevents the Steam client libraries
    # from being found.
    UMU_PATCH_DIR="$(mktemp -d)"
    (cd "${UMU_PATCH_DIR}" && "${BUILD_PY}" << PATCHEOF
import zipfile, pathlib
zf = zipfile.ZipFile('/src/${RUNDIR}/umu-run')
zf.extractall('src')
run_py = pathlib.Path('src/umu/umu_run.py')
src = run_py.read_text()

# Patch 1: preserve STEAM_COMPAT_CLIENT_INSTALL_PATH
old1 = '    env["STEAM_COMPAT_INSTALL_PATH"] = os.environ.get("STEAM_COMPAT_INSTALL_PATH", "")'
new1 = (old1
    + '\n    env["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = os.environ.get('
    + '\n        "STEAM_COMPAT_CLIENT_INSTALL_PATH", ""'
    + '\n    )')
if old1 in src and 'STEAM_COMPAT_CLIENT_INSTALL_PATH"] = os.environ' not in src:
    src = src.replace(old1, new1)

run_py.write_text(src)
PATCHEOF
)
    "${BUILD_PY}" -m zipapp "${UMU_PATCH_DIR}/src" -o "${OUT_DIR}/umu-run" -p '/usr/bin/env python3'
    chmod +x "${OUT_DIR}/umu-run"
    rm -rf "${UMU_PATCH_DIR}"
fi
[ -f "${RUNDIR}/README-PORTABLE.txt" ] && cp -f "${RUNDIR}/README-PORTABLE.txt" "${OUT_DIR}/"
[ -f "/src/src/fluorine-manager" ] && cp -f "/src/src/fluorine-manager" "${OUT_DIR}/"

# lootcli (spawned by MO2 for load-order sorting).
LOOTCLI="build/libs/lootcli/src/lootcli"
[ -f "${LOOTCLI}" ] && cp -f "${LOOTCLI}" "${OUT_DIR}/"

for tool in wrestool icotool; do
    command -v "${tool}" >/dev/null 2>&1 && cp -f "$(command -v "${tool}")" "${OUT_DIR}/"
done

# ── MO2 plugins (.so) ──
find build/libs -type f \( \
    -name "libgame_*.so" -o \
    -name "libinstaller_*.so" -o \
    -name "libfomod_plus_*.so" -o \
    -name "libpreview_*.so" -o \
    -name "libdiagnose_*.so" -o \
    -name "libcheck_*.so" -o \
    -name "libtool_*.so" -o \
    -name "libinieditor.so" -o \
    -name "libinibakery.so" -o \
    -name "libbsa_extractor.so" -o \
    -name "libbsa_packer.so" -o \
    -name "libproxy.so" \
\) -exec cp -f {} "${OUT_DIR}/plugins/" \;

# Python plugin payload.
for f in libplugin_python.so lzokay.py winreg.py pyCfg.py \
         DDSPreview.py Form43Checker.py ScriptExtenderPluginChecker.py; do
    [ -f "build/src/src/plugins/${f}" ] && cp -f "build/src/src/plugins/${f}" "${OUT_DIR}/plugins/"
done
for d in basic_games data libs dlls; do
    [ -d "build/src/src/plugins/${d}" ] && cp -a "build/src/src/plugins/${d}" "${OUT_DIR}/plugins/"
done
rm -f "${OUT_DIR}/plugins/FNIS"*.py

# Source-tree Python plugins (OMOD installer, etc.).
for f in /src/src/plugins/*.py; do
    [ -f "${f}" ] && cp -f "${f}" "${OUT_DIR}/plugins/"
done

# ── Stylesheets (themes) ──
if [ -d "build/src/src/stylesheets" ]; then
    cp -a "build/src/src/stylesheets" "${OUT_DIR}/"
    echo "Bundled stylesheets"
fi

# ── 7z runtime ──
SO7="build/src/src/dlls/7z.so"
if [ -f "${SO7}" ]; then
    cp -f "${SO7}" "${OUT_DIR}/dlls/7z.so"
    cp -f "${SO7}" "${OUT_DIR}/dlls/7zip.dll"
fi

# ── Project-specific shared libraries ──
cp -f build/libs/uibase/src/libuibase.so "${OUT_DIR}/lib/"
cp -f build/libs/libbsarch/liblibbsarch.so "${OUT_DIR}/lib/"
cp -f build/libs/archive/src/libarchive.so "${OUT_DIR}/lib/"
cp -f build/libs/plugin_python/src/runner/librunner.so "${OUT_DIR}/lib/"
for ffi in libs/bsa_ffi/target/release/libbsa_ffi.so \
           libs/nak_ffi/target/release/libnak_ffi.so; do
    [ -f "${ffi}" ] && cp -f "${ffi}" "${OUT_DIR}/lib/"
done

# Boost (version-pinned to container, won't exist on most user systems).
for boost_lib in /lib/x86_64-linux-gnu/libboost_program_options.so* \
                 /lib/x86_64-linux-gnu/libboost_thread.so*; do
    [ -f "${boost_lib}" ] && cp -Lf "${boost_lib}" "${OUT_DIR}/lib/"
done

# ── Bundle ALL shared library dependencies ──
# Collect every .so the binary and plugins link against, then bundle
# everything except core glibc/system libs (which must come from the host).
echo "Bundling shared library dependencies..."

# Libraries that MUST come from the host (glibc, GPU drivers, etc.)
SKIP_PATTERN="linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|librt\.so|libpthread|libresolv|libnss|libgcc_s|libstdc\+\+"
# GPU/graphics drivers must be host-provided
SKIP_PATTERN="${SKIP_PATTERN}|libGL\.so|libEGL|libGLX|libGLdispatch|libdrm|libvulkan|libX11|libxcb|libwayland-client|libwayland-server|libwayland-cursor|libwayland-egl|libxkbcommon"
# libpython is shipped by the portable Python runtime in python/lib/ — do NOT
# copy the container's version into lib/ or it will shadow the portable one.
SKIP_PATTERN="${SKIP_PATTERN}|libpython"

collect_deps() {
    ldd "$1" 2>/dev/null | grep "=>" | awk '{print $3}' | grep "^/" | sort -u
}

ALL_DEPS=$(mktemp)
# Main binary
collect_deps "${OUT_DIR}/ModOrganizer-core" >> "${ALL_DEPS}"
# All plugin .so files
find "${OUT_DIR}/plugins" -name "*.so" -exec sh -c 'ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/"' _ {} \; >> "${ALL_DEPS}"
# Our own libs
find "${OUT_DIR}/lib" -name "*.so*" -exec sh -c 'ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/"' _ {} \; >> "${ALL_DEPS}"
# lootcli
[ -f "${OUT_DIR}/lootcli" ] && collect_deps "${OUT_DIR}/lootcli" >> "${ALL_DEPS}"

sort -u "${ALL_DEPS}" | while read -r dep; do
    dep_name="$(basename "${dep}")"
    # Skip system libs
    if echo "${dep_name}" | grep -qE "${SKIP_PATTERN}"; then
        continue
    fi
    # Skip if already bundled
    if [ -f "${OUT_DIR}/lib/${dep_name}" ]; then
        continue
    fi
    cp -Lf "${dep}" "${OUT_DIR}/lib/" 2>/dev/null || true
done
rm -f "${ALL_DEPS}"
# Remove any libpython that leaked into lib/ — must come from python/lib/ only.
rm -f "${OUT_DIR}/lib"/libpython*.so* 2>/dev/null || true
echo "Dependencies bundled."

# ── Qt6 platform plugins ──
QT6_PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt6/plugins"
if [ ! -d "${QT6_PLUGIN_DIR}" ]; then
    QT6_PLUGIN_DIR="$(qtpaths6 --plugin-dir 2>/dev/null || echo "")"
fi
if [ -d "${QT6_PLUGIN_DIR}" ]; then
    mkdir -p "${OUT_DIR}/qt6plugins"
    for plugin_type in platforms tls networkinformation styles \
                       wayland-shell-integration \
                       wayland-decoration-client wayland-graphics-integration-client \
                       platformthemes imageformats iconengines xcbglintegrations \
                       egldeviceintegrations; do
        if [ -d "${QT6_PLUGIN_DIR}/${plugin_type}" ]; then
            cp -a "${QT6_PLUGIN_DIR}/${plugin_type}" "${OUT_DIR}/qt6plugins/"
        fi
    done
    # Bundle deps of Qt plugins too
    find "${OUT_DIR}/qt6plugins" -name "*.so" -exec sh -c '
        ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/" | while read dep; do
            dep_name="$(basename "${dep}")"
            echo "${dep_name}" | grep -qE "'"${SKIP_PATTERN}"'" && continue
            [ -f "'"${OUT_DIR}"'/lib/${dep_name}" ] && continue
            cp -Lf "${dep}" "'"${OUT_DIR}"'/lib/" 2>/dev/null || true
        done
    ' _ {} \;
    echo "Bundled Qt6 plugins from ${QT6_PLUGIN_DIR}"
else
    echo "WARNING: Could not find Qt6 plugin directory"
fi

# libloot (custom-built, never on user systems).
if [ -f /usr/local/lib/libloot.so.0 ]; then
    cp -Lf /usr/local/lib/libloot.so.0 "${OUT_DIR}/lib/"
    # Create the unversioned symlink too.
    ln -sf libloot.so.0 "${OUT_DIR}/lib/libloot.so"
fi

# ── Portable Python runtime ──
PORTABLE_PY="/opt/portable-python"
if [ -d "${PORTABLE_PY}" ]; then
    echo "Bundling portable Python from ${PORTABLE_PY}..."
    cp -a "${PORTABLE_PY}" "${OUT_DIR}/python"

    # Trim unnecessary files from portable Python.
    PP_STDLIB="${OUT_DIR}/python/lib/python${PY_MM}"
    rm -rf "${PP_STDLIB}/test" \
           "${PP_STDLIB}/unittest/test" \
           "${PP_STDLIB}/idlelib" \
           "${PP_STDLIB}/tkinter" \
           "${PP_STDLIB}/turtledemo" \
           "${PP_STDLIB}/__pycache__" \
           "${OUT_DIR}/python/include" \
           "${OUT_DIR}/python/share" \
           2>/dev/null || true
    find "${OUT_DIR}/python" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
    find "${OUT_DIR}/python" -name "*.pyc" -delete 2>/dev/null || true

    # Ensure versioned soname symlink exists (pybind11 links against libpython3.13.so.1.0).
    if [ -f "${OUT_DIR}/python/lib/libpython${PY_MM}.so" ] && \
       [ ! -f "${OUT_DIR}/python/lib/libpython${PY_MM}.so.1.0" ]; then
        ln -sf "libpython${PY_MM}.so" "${OUT_DIR}/python/lib/libpython${PY_MM}.so.1.0"
    fi
else
    echo "ERROR: Portable Python not found at ${PORTABLE_PY}"
    exit 1
fi

# Bundle PyQt6 from system into portable Python's site-packages.
PYSITE="${OUT_DIR}/python/lib/python${PY_MM}/site-packages"
mkdir -p "${PYSITE}"
for search_dir in /usr/lib/python3/dist-packages \
                  "/usr/lib/python${PY_MM}/dist-packages" \
                  "/usr/local/lib/python${PY_MM}/dist-packages"; do
    if [ -d "${search_dir}/PyQt6" ]; then
        echo "Bundling PyQt6 from ${search_dir}..."
        cp -a "${search_dir}/PyQt6" "${PYSITE}/"
        [ -d "${search_dir}/PyQt6_sip" ] && cp -a "${search_dir}/PyQt6_sip" "${PYSITE}/"
        [ -d "${search_dir}/sip" ] && cp -a "${search_dir}/sip" "${PYSITE}/"
        break
    fi
done

# Install Python packages into portable runtime via uv.
uv pip install --python "${OUT_DIR}/python/bin/python3" psutil vdf

# Build-tree Python plugin payload.
[ -d build/src/src/python ] && cp -a build/src/src/python/. "${OUT_DIR}/python/"

# ── Strip all MO2 binaries (not portable Python) ──
echo "Stripping MO2 binaries..."
strip --strip-unneeded "${OUT_DIR}/ModOrganizer-core" 2>/dev/null || true
find "${OUT_DIR}/plugins" -name "*.so" -exec strip --strip-unneeded {} \; 2>/dev/null || true
find "${OUT_DIR}/dlls" -name "*.so" -o -name "*.dll" | xargs -r strip --strip-unneeded 2>/dev/null || true
find "${OUT_DIR}/lib" -name "*.so" -exec strip --strip-unneeded {} \; 2>/dev/null || true
for tool in wrestool icotool lootcli; do
    [ -f "${OUT_DIR}/${tool}" ] && strip --strip-unneeded "${OUT_DIR}/${tool}" 2>/dev/null || true
done

# ── Fix RPATH so binaries find libs without LD_LIBRARY_PATH ──
echo "Patching RPATH..."
patchelf --set-rpath '$ORIGIN/lib' "${OUT_DIR}/ModOrganizer-core"
[ -f "${OUT_DIR}/lootcli" ] && patchelf --set-rpath '$ORIGIN/lib' "${OUT_DIR}/lootcli"
find "${OUT_DIR}/plugins" -name "*.so" -exec patchelf --set-rpath '$ORIGIN/../lib' {} \; 2>/dev/null || true

# ── Validate embedded Python runtime ──
if ! PYTHONHOME="${OUT_DIR}/python" \
     PYTHONPATH="${OUT_DIR}/python/lib/python${PY_MM}:${PYSITE}" \
     LD_LIBRARY_PATH="${OUT_DIR}/lib:${OUT_DIR}/python/lib:${LD_LIBRARY_PATH:-}" \
     "${OUT_DIR}/python/bin/python3" -c \
     "import zlib; import runpy; import zipimport; print('python embed check ok')"; then
    echo "ERROR: Embedded Python runtime check failed."
    exit 1
fi

# ── Launcher script ──
cat > "${OUT_DIR}/fluorine-manager" <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
SELF="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$SELF")" && pwd)"
export PATH="${HERE}:${PATH}"
export LD_LIBRARY_PATH="${HERE}/lib:${HERE}/python/lib:${LD_LIBRARY_PATH:-}"
export MO2_BASE_DIR="${HERE}"
export MO2_PLUGINS_DIR="${HERE}/plugins"
export MO2_DLLS_DIR="${HERE}/dlls"
export MO2_PYTHON_DIR="${HERE}/python"
# PYTHONHOME is set only for the MO2 process (not exported to children like
# umu-run/Proton which have their own Python).  MO2_PYTHON_DIR lets the
# binary reconstruct it internally.
MO2_PYTHONHOME="${HERE}/python"
unset PYTHONPATH PYTHONNOUSERSITE PYTHONHOME

# Use bundled Qt6 plugins.
export QT_PLUGIN_PATH="${HERE}/qt6plugins"

cd "${HERE}"
exec env PYTHONHOME="${MO2_PYTHONHOME}" "${HERE}/ModOrganizer-core" "$@"
LAUNCH
chmod +x "${OUT_DIR}/fluorine-manager"

# ── Desktop integration files (for AppImage) ──
cp -f /src/data/com.fluorine.manager.desktop "${OUT_DIR}/"
cp -f /src/data/com.fluorine.manager.png "${OUT_DIR}/"
cp -f /src/data/com.fluorine.manager.metainfo.xml "${OUT_DIR}/"

# ── Build AppImage ──
echo ""
echo "=== Building AppImage ==="

APPDIR="/src/build/AppDir"
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib" "${APPDIR}/usr/share/applications" \
         "${APPDIR}/usr/plugins" \
         "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
         "${APPDIR}/usr/share/metainfo"

# Copy staging into AppDir layout
cp -a "${OUT_DIR}"/. "${APPDIR}/usr/bin/"
# Move libs to standard location
mv "${APPDIR}/usr/bin/lib"/* "${APPDIR}/usr/lib/" 2>/dev/null || true
rmdir "${APPDIR}/usr/bin/lib" 2>/dev/null || true
# Flatpak runtime exposed Qt plugins from a stable system location. Mirror that
# layout in AppImage by placing bundled Qt plugins under /usr/plugins.
if [ -d "${APPDIR}/usr/bin/qt6plugins" ]; then
    cp -a "${APPDIR}/usr/bin/qt6plugins"/. "${APPDIR}/usr/plugins/"
fi

# Desktop integration
cp -f "${OUT_DIR}/com.fluorine.manager.desktop" "${APPDIR}/usr/share/applications/"
cp -f "${OUT_DIR}/com.fluorine.manager.png" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/"
cp -f "${OUT_DIR}/com.fluorine.manager.metainfo.xml" "${APPDIR}/usr/share/metainfo/"

# Bundle icon themes so QIcon::fromTheme() calls resolve inside the AppImage.
# Flatpak runtime provided this globally; AppImage must carry it explicitly.
mkdir -p "${APPDIR}/usr/share/icons"
for theme_dir in /usr/share/icons/*; do
    [ -d "${theme_dir}" ] || continue
    cp -a "${theme_dir}" "${APPDIR}/usr/share/icons/"
done
if [ -f "/usr/share/icons/default/index.theme" ]; then
    mkdir -p "${APPDIR}/usr/share/icons/default"
    cp -f "/usr/share/icons/default/index.theme" "${APPDIR}/usr/share/icons/default/"
fi

# Update RPATH for new lib location
patchelf --set-rpath '$ORIGIN/../lib' "${APPDIR}/usr/bin/ModOrganizer-core"
[ -f "${APPDIR}/usr/bin/lootcli" ] && patchelf --set-rpath '$ORIGIN/../lib' "${APPDIR}/usr/bin/lootcli"
find "${APPDIR}/usr/bin/plugins" -name "*.so" -exec patchelf --set-rpath '$ORIGIN/../../lib' {} \; 2>/dev/null || true

# Create AppRun wrapper
cat > "${APPDIR}/AppRun" <<'APPRUN'
#!/usr/bin/env bash
set -euo pipefail
SELF="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$SELF")" && pwd)"
BIN="${HERE}/usr/bin"
APPIMAGE_DIR="$(dirname "$(readlink -f "${APPIMAGE:-$0}")")"

sync_dir() {
    local name="$1"
    local src="${BIN}/${name}"
    local dst="${APPIMAGE_DIR}/${name}"
    [ -d "${src}" ] || return 0
    mkdir -p "${dst}"
    (cd "${src}" && tar --exclude-vcs -cf - .) | (cd "${dst}" && tar -xf -)
}

# Keep runtime payload outside the transient AppImage mount.
sync_dir "plugins"
sync_dir "dlls"
sync_dir "python"

export PATH="${BIN}:${PATH}"
if [ -d "${APPIMAGE_DIR}/python/lib" ]; then
    export LD_LIBRARY_PATH="${HERE}/usr/lib:${APPIMAGE_DIR}/python/lib:${LD_LIBRARY_PATH:-}"
else
    export LD_LIBRARY_PATH="${HERE}/usr/lib:${BIN}/python/lib:${LD_LIBRARY_PATH:-}"
fi

export MO2_BASE_DIR="${APPIMAGE_DIR}"
export MO2_PLUGINS_DIR="${APPIMAGE_DIR}/plugins"
export MO2_DLLS_DIR="${APPIMAGE_DIR}/dlls"
if [ -d "${APPIMAGE_DIR}/python/lib" ]; then
    export MO2_PYTHON_DIR="${APPIMAGE_DIR}/python"
else
    export MO2_PYTHON_DIR="${BIN}/python"
fi

unset PYTHONPATH PYTHONNOUSERSITE PYTHONHOME

# Use bundled Qt6 plugins.
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="${HERE}/usr/plugins/platforms"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export QT_ICON_THEME_NAME="${QT_ICON_THEME_NAME:-breeze}"
export QT_STYLE_OVERRIDE="${QT_STYLE_OVERRIDE:-Breeze}"
export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-KDE}"

cd "${APPIMAGE_DIR}"
exec "${BIN}/ModOrganizer-core" "$@"
APPRUN
chmod +x "${APPDIR}/AppRun"

# Symlinks required by AppImage spec
ln -sf usr/share/applications/com.fluorine.manager.desktop "${APPDIR}/com.fluorine.manager.desktop"
ln -sf usr/share/icons/hicolor/256x256/apps/com.fluorine.manager.png "${APPDIR}/com.fluorine.manager.png"
ln -sf usr/share/icons/hicolor/256x256/apps/com.fluorine.manager.png "${APPDIR}/.DirIcon"

# Extract linuxdeploy (can't use FUSE inside Docker)
DEPLOY_DIR="/tmp/linuxdeploy-extract"
if [ ! -d "${DEPLOY_DIR}" ]; then
    cd /opt/linuxdeploy
    ./linuxdeploy-x86_64.AppImage --appimage-extract >/dev/null
    mv squashfs-root "${DEPLOY_DIR}"
    chmod +x "${DEPLOY_DIR}/AppRun"
fi

# Use linuxdeploy to generate the AppImage.
# We skip the Qt plugin since we already handle Qt plugin paths at runtime
# and our deps are already staged.
export ARCH=x86_64
"${DEPLOY_DIR}/AppRun" \
    --appdir "${APPDIR}" \
    --output appimage \
    --desktop-file "${APPDIR}/usr/share/applications/com.fluorine.manager.desktop" \
    --icon-file "${APPDIR}/usr/share/icons/hicolor/256x256/apps/com.fluorine.manager.png" \
    2>&1 || {
    echo "WARNING: linuxdeploy AppImage generation failed, falling back to staging dir output"
}

# Move AppImage to output
APPIMAGE_FILE=$(ls -1 Fluorine*.AppImage 2>/dev/null || ls -1 *.AppImage 2>/dev/null || true)
if [ -n "${APPIMAGE_FILE}" ]; then
    mv "${APPIMAGE_FILE}" "/src/build/"
    echo ""
    echo "=== AppImage built ==="
    ls -lh "/src/build/"*.AppImage
else
    echo ""
    echo "=== Staging complete (AppImage generation skipped) ==="
fi

echo ""
echo "=== Build Summary ==="
du -sh "${OUT_DIR}"/*/ "${OUT_DIR}"/ModOrganizer-core 2>/dev/null | sort -rh
