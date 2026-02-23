#!/usr/bin/env bash
set -euo pipefail

BUILD_PY="${BUILD_PYTHON:-$(command -v python3)}"

# ── Build ──
PYBIND11_DIR="$("${BUILD_PY}" -c 'import pybind11; print(pybind11.get_cmake_dir())' 2>/dev/null || true)"

CMAKE_EXTRA_ARGS=()
if [ -n "${CMAKE_C_COMPILER_LAUNCHER:-}" ]; then
    CMAKE_EXTRA_ARGS+=("-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}")
fi
if [ -n "${CMAKE_CXX_COMPILER_LAUNCHER:-}" ]; then
    CMAKE_EXTRA_ARGS+=("-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
fi

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPython_EXECUTABLE="${BUILD_PY}" \
    ${PYBIND11_DIR:+-Dpybind11_DIR="${PYBIND11_DIR}"} \
    -DBUILD_PLUGIN_PYTHON=ON \
    "${CMAKE_EXTRA_ARGS[@]}"

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
# PyQt6 extension modules — these link against Qt6 libs that MO2 binaries don't
# directly depend on (e.g. libQt6OpenGLWidgets, libQt6PrintSupport).  Without
# bundling these, PyQt6 falls back to host Qt which may be a different version.
# Scan from the system dist-packages (portable Python isn't copied yet).
for pyqt_search in /usr/lib/python3/dist-packages/PyQt6 \
                   "/usr/lib/python${PY_MM}/dist-packages/PyQt6" \
                   "/usr/local/lib/python${PY_MM}/dist-packages/PyQt6"; do
    if [ -d "${pyqt_search}" ]; then
        find "${pyqt_search}" -name "*.so" -exec sh -c 'ldd "$1" 2>/dev/null | grep "=>" | awk "{print \$3}" | grep "^/"' _ {} \; >> "${ALL_DEPS}"
        break
    fi
done

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

    # The portable Python's libpython has SONAME "libpython3.13.so", but
    # pybind11::embed links librunner.so against the build-venv's libpython
    # which may have SONAME "libpython3.13.so.1.0".  A SONAME mismatch causes
    # two copies of libpython to be loaded at runtime, making
    # Py_IsInitialized() return false after Py_InitializeFromConfig() succeeds.
    #
    # Fix: patch the portable Python's SONAME to include the .1.0 suffix,
    # matching what the linker recorded in librunner.so's DT_NEEDED.
    PP_LIBPY="${OUT_DIR}/python/lib/libpython${PY_MM}.so"
    if [ -f "${PP_LIBPY}" ]; then
        CURRENT_SONAME="$(readelf -d "${PP_LIBPY}" 2>/dev/null | grep SONAME | sed 's/.*\[//' | sed 's/\]//')"
        echo "Portable Python SONAME: ${CURRENT_SONAME}"
        if [ "${CURRENT_SONAME}" = "libpython${PY_MM}.so" ]; then
            echo "Patching SONAME to libpython${PY_MM}.so.1.0 ..."
            patchelf --set-soname "libpython${PY_MM}.so.1.0" "${PP_LIBPY}"
            # Rename the file to match and create backwards symlink.
            mv "${PP_LIBPY}" "${PP_LIBPY}.1.0"
            ln -sf "libpython${PY_MM}.so.1.0" "${PP_LIBPY}"
        fi
        # Ensure the .1.0 name exists (either as the real file or a symlink).
        if [ ! -e "${PP_LIBPY}.1.0" ]; then
            ln -sf "libpython${PY_MM}.so" "${PP_LIBPY}.1.0"
        fi
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
patchelf --set-rpath '$ORIGIN/lib:$ORIGIN/python/lib' "${OUT_DIR}/ModOrganizer-core"
[ -f "${OUT_DIR}/lootcli" ] && patchelf --set-rpath '$ORIGIN/lib' "${OUT_DIR}/lootcli"
find "${OUT_DIR}/plugins" -name "*.so" -exec patchelf --set-rpath '$ORIGIN/../lib:$ORIGIN/../python/lib' {} \; 2>/dev/null || true
# Libraries in lib/ (e.g. librunner.so) need to find sibling libs and python/lib.
find "${OUT_DIR}/lib" -name "*.so" -exec patchelf --set-rpath '$ORIGIN:$ORIGIN/../python/lib' {} \; 2>/dev/null || true

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

# ── Sync portable Python to data dir (only when outdated) ──
FLUORINE_DATA="${HOME}/.local/share/fluorine"
PYTHON_DST="${FLUORINE_DATA}/python"
PYTHON_SRC="${HERE}/python"

if [ -d "${PYTHON_SRC}" ]; then
    CURRENT_VER="$(stat -c '%i:%Y' "${PYTHON_SRC}/bin/python3" 2>/dev/null || echo "unknown")"
    MARKER="${PYTHON_DST}/.version"

    if [ ! -f "${MARKER}" ] || [ "$(cat "${MARKER}" 2>/dev/null)" != "${CURRENT_VER}" ]; then
        echo "Syncing Python runtime to ${PYTHON_DST}..." >&2
        rm -rf "${PYTHON_DST}"
        mkdir -p "${PYTHON_DST}"
        (cd "${PYTHON_SRC}" && tar --exclude-vcs -cf - .) | (cd "${PYTHON_DST}" && tar -xf -)
        echo "${CURRENT_VER}" > "${MARKER}"
    fi
fi

export LD_LIBRARY_PATH="${HERE}/lib:${PYTHON_DST}/lib:${LD_LIBRARY_PATH:-}"
export MO2_BASE_DIR="${HERE}"
export MO2_PLUGINS_DIR="${HERE}/plugins"
export MO2_DLLS_DIR="${HERE}/dlls"
export MO2_PYTHON_DIR="${PYTHON_DST}"
# PYTHONHOME is set only for the MO2 process (not exported to children like
# Proton which has its own Python).  MO2_PYTHON_DIR lets the
# binary reconstruct it internally.
MO2_PYTHONHOME="${PYTHON_DST}"
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

# Symlink the portable Python's libpython into usr/lib/ so that:
# 1) linuxdeploy sees the dependency as already satisfied and doesn't bundle
#    the container's system libpython (which lacks static built-in modules)
# 2) librunner.so (which lives in usr/lib/) can resolve libpython via $ORIGIN
PP_APPDIR_LIB="${APPDIR}/usr/bin/python/lib"
for pp_lib in "${PP_APPDIR_LIB}"/libpython*.so*; do
    [ -e "${pp_lib}" ] || continue
    pp_name="$(basename "${pp_lib}")"
    # Remove any copy linuxdeploy might have placed, then symlink to portable.
    rm -f "${APPDIR}/usr/lib/${pp_name}"
    ln -sf ../bin/python/lib/"${pp_name}" "${APPDIR}/usr/lib/${pp_name}"
done

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

# Update RPATH for new lib location.
# usr/lib/ contains librunner.so and other MO2 libs.  The symlinks above make
# libpython resolvable via $ORIGIN for libraries in usr/lib/.
patchelf --set-rpath '$ORIGIN/../lib:$ORIGIN/python/lib' "${APPDIR}/usr/bin/ModOrganizer-core"
[ -f "${APPDIR}/usr/bin/lootcli" ] && patchelf --set-rpath '$ORIGIN/../lib' "${APPDIR}/usr/bin/lootcli"
find "${APPDIR}/usr/bin/plugins" -name "*.so" -exec patchelf --set-rpath '$ORIGIN/../../lib' {} \; 2>/dev/null || true
find "${APPDIR}/usr/lib" -name "*.so" -not -name "libpython*" -exec patchelf --set-rpath '$ORIGIN' {} \; 2>/dev/null || true

# Create AppRun wrapper
cat > "${APPDIR}/AppRun" <<'APPRUN'
#!/usr/bin/env bash
set -euo pipefail
SELF="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$SELF")" && pwd)"
BIN="${HERE}/usr/bin"
APPIMAGE_DIR="$(dirname "$(readlink -f "${APPIMAGE:-$0}")")"

# ── Sync portable Python to data dir (only when outdated) ──
# Plugins and dlls are read-only and loaded directly from the squashfs mount.
# Python needs a writable copy because pip/plugins may modify site-packages.
FLUORINE_DATA="${HOME}/.local/share/fluorine"
PYTHON_DST="${FLUORINE_DATA}/python"
PYTHON_SRC="${BIN}/python"

if [ -d "${PYTHON_SRC}" ]; then
    APPIMAGE_REAL="$(readlink -f "${APPIMAGE:-$0}")"
    CURRENT_VER="$(stat -c '%i:%Y' "${APPIMAGE_REAL}" 2>/dev/null || echo "unknown")"
    MARKER="${PYTHON_DST}/.version"

    if [ ! -f "${MARKER}" ] || [ "$(cat "${MARKER}" 2>/dev/null)" != "${CURRENT_VER}" ]; then
        echo "Syncing Python runtime to ${PYTHON_DST}..."
        rm -rf "${PYTHON_DST}"
        mkdir -p "${PYTHON_DST}"
        (cd "${PYTHON_SRC}" && tar --exclude-vcs -cf - .) | (cd "${PYTHON_DST}" && tar -xf -)
        echo "${CURRENT_VER}" > "${MARKER}"
    fi
fi

# Save the original (pre-AppImage) environment so game launches can restore it.
# Without this, AppImage's LD_LIBRARY_PATH/PATH leak into Proton and
# cause library conflicts that make games crash.
export FLUORINE_ORIG_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export FLUORINE_ORIG_PATH="${PATH}"
export FLUORINE_ORIG_XDG_DATA_DIRS="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export FLUORINE_ORIG_QT_PLUGIN_PATH="${QT_PLUGIN_PATH:-}"

export PATH="${BIN}:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${PYTHON_DST}/lib:${LD_LIBRARY_PATH:-}"

export MO2_BASE_DIR="${APPIMAGE_DIR}"
export MO2_PLUGINS_DIR="${BIN}/plugins"
export MO2_DLLS_DIR="${BIN}/dlls"
export MO2_PYTHON_DIR="${PYTHON_DST}"

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
#
# linuxdeploy scans binary deps and bundles them.  We must prevent it from
# bundling the container's system libpython (which lacks statically-compiled
# extension modules like _ctypes, resource, _lzma).  The portable Python
# runtime in usr/bin/python/lib/ is the correct copy.  Symlinks in usr/lib/
# point to it, and --exclude-library prevents linuxdeploy from overwriting them.
export ARCH=x86_64
# Tell linuxdeploy to skip libpython — we bundle the portable Python's copy
# via symlinks in usr/lib/ and must not replace it with the container's version.
export LINUXDEPLOY_EXCLUDE_MODULES="libpython"
"${DEPLOY_DIR}/AppRun" \
    --appdir "${APPDIR}" \
    --output appimage \
    --desktop-file "${APPDIR}/usr/share/applications/com.fluorine.manager.desktop" \
    --icon-file "${APPDIR}/usr/share/icons/hicolor/256x256/apps/com.fluorine.manager.png" \
    --exclude-library "libpython*" \
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
