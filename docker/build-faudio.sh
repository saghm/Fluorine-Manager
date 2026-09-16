#!/usr/bin/env bash
set -euo pipefail

# Wine's XACT/XAudio PE DLLs statically link FAudio. Build both architectures
# from source so the bundled DLLs actually contain the requested FAudio code.
OUT_DIR="${1:?usage: build-faudio.sh OUTPUT_DIRECTORY}"
CACHE_DIR="${FAUDIO_BUILD_CACHE:-/src/build/faudio-build}"
JOBS="${BUILD_JOBS:-8}"
SAFE_FAUDIO=26.02
WINE_TAG=wine-11.17
PATCH_DIR="$(cd "$(dirname "$0")" && pwd)/patches/faudio"
SCRIPT_DIR="$(dirname "${PATCH_DIR}")/.."
RECIPE=$(cat "$0" "${PATCH_DIR}"/*.patch | sha256sum | cut -c1-16)

mkdir -p "${CACHE_DIR}" "${OUT_DIR}"

if [ -n "${FLUORINE_FAUDIO_LATEST_TAG:-}" ]; then
    LATEST_FAUDIO="${FLUORINE_FAUDIO_LATEST_TAG}"
else
    LATEST_FAUDIO="$(curl -fsSL --retry 3 \
        https://api.github.com/repos/FNA-XNA/FAudio/releases/latest \
        | python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])')"
fi
if [[ ! "${LATEST_FAUDIO}" =~ ^[0-9]{2}\.[0-9]{2}$ ]]; then
    echo "ERROR: Unexpected latest FAudio release tag: ${LATEST_FAUDIO}" >&2
    exit 1
fi

fetch_source() {
    local repo="$1" revision="$2" archive="$3" source="$4"
    if [ ! -s "${archive}" ]; then
        curl -fL --retry 3 \
            "https://codeload.github.com/${repo}/tar.gz/${revision}" \
            -o "${archive}.part"
        mv -f "${archive}.part" "${archive}"
    fi
    if [ ! -d "${source}" ]; then
        mkdir -p "${source}.part"
        tar -xf "${archive}" -C "${source}.part" --strip-components=1
        mv "${source}.part" "${source}"
    fi
}

resolve_tag() {
    git ls-remote "https://github.com/$1.git" "refs/tags/$2" "refs/tags/$2^{}" \
        | LC_ALL=C sort -k2 | tail -1 | cut -f1
}

WINE_REVISION=$(resolve_tag wine-mirror/wine "${WINE_TAG}")

apply_correction() {
    local source="$1" correction="$2"
    if patch --batch --fuzz=0 --dry-run -d "${source}" -p1 < "${PATCH_DIR}/${correction}" >/dev/null; then
        patch --batch --fuzz=0 -d "${source}" -p1 < "${PATCH_DIR}/${correction}"
    elif ! patch --batch --fuzz=0 --dry-run -R -d "${source}" -p1 < "${PATCH_DIR}/${correction}" >/dev/null; then
        echo "ERROR: Review ${correction}; patch no longer applies to ${source}" >&2
        exit 1
    fi
}

build_variant() {
    local variant="$1" faudio_tag="$2"
    local faudio_revision
    faudio_revision=$(resolve_tag FNA-XNA/FAudio "${faudio_tag}")
    if [[ ! "${faudio_revision}" =~ ^[a-f0-9]{40}$ || ! "${WINE_REVISION}" =~ ^[a-f0-9]{40}$ ]]; then
        echo "ERROR: Could not resolve source revisions" >&2
        exit 1
    fi
    local wine_tag="${WINE_TAG}"
    local work="${CACHE_DIR}/${variant}-${faudio_revision}-${RECIPE}"
    local wine_archive="${CACHE_DIR}/${WINE_REVISION}.tar.gz"
    local faudio_archive="${CACHE_DIR}/${faudio_revision}.tar.gz"
    local wine_source="${work}/wine-source"
    local faudio_source="${work}/faudio-source"
    local wine_build="${work}/wine-build"

    mkdir -p "${work}"
    fetch_source wine-mirror/wine "${WINE_REVISION}" "${wine_archive}" "${wine_source}"
    fetch_source FNA-XNA/FAudio "${faudio_revision}" "${faudio_archive}" "${faudio_source}"

    if [ ! -f "${work}/prepared" ]; then
        apply_correction "${wine_source}" 0003-audio-ordinary-pe.patch
        apply_correction "${wine_source}" 0007-reverb-parameter-trace.patch
        if [ "${variant}" = latest ]; then
            for correction in 0001-wma-bytes-required.patch 0002-refresh-queued-buffer.patch \
                              0004-refresh-after-callbacks.patch 0005-preserve-lookahead-samples.patch \
                              0006-reverb-delay-boundaries.patch 0008-isolate-send-filters.patch; do
                apply_correction "${faudio_source}" "${correction}"
            done
        fi
        # Both variants use the same wrappers, with the exact upstream FAudio
        # revision overlaid and only Wine's static-library adaptations below.
        cp -a "${faudio_source}/include/." "${wine_source}/libs/faudio/include/"
        cp -a "${faudio_source}/src/." "${wine_source}/libs/faudio/src/"
        python3 - "${wine_source}/libs/faudio" <<'ADAPT'
from pathlib import Path
import sys
root = Path(sys.argv[1])
for path in (root / 'include').glob('*.h'):
    text = path.read_text().replace('__declspec(dllexport)', '')
    path.write_text(text)
path = root / 'src/FAudio_platform_win32.c'
text = path.read_text().replace('DEFINE_MEDIATYPE_GUID(MFAudioFormat_XMAudio2, FAUDIO_FORMAT_XMAUDIO2);\n', '')
path.write_text(text)
ADAPT
        touch "${work}/prepared"
    fi

    if [ "${variant}" = latest ]; then
        bash "${SCRIPT_DIR}/test-faudio.sh" "${faudio_source}" "${work}/tests"
    fi

    if [ ! -f "${wine_build}/Makefile" ]; then
        mkdir -p "${wine_build}"
        (cd "${wine_build}" && "${wine_source}/configure" \
            --enable-archs=i386,x86_64 --disable-tests --without-x \
            > configure.log 2>&1) || {
            tail -60 "${wine_build}/configure.log" >&2
            exit 1
        }
    fi

    local targets=() dll_dir
    while IFS= read -r dll_dir; do
        targets+=("dlls/$(basename "${dll_dir}")/all")
    done < <(find "${wine_source}/dlls" -mindepth 1 -maxdepth 1 -type d \
        \( -name 'xactengine2_*' -o -name 'xactengine3_*' \
           -o -name 'x3daudio1_*' -o -name 'xapofx1_*' \
           -o -name 'xaudio2_[0-9]' \) | LC_ALL=C sort)

    echo "Building FAudio ${faudio_tag} via ${wine_tag} (${variant}, x86/x64)..."
    if ! make -C "${wine_build}" -j "${JOBS}" "${targets[@]}" \
        > "${wine_build}/audio-build.log" 2>&1; then
        tail -80 "${wine_build}/audio-build.log" >&2
        exit 1
    fi

    local variant_dir="${OUT_DIR}/${variant}" arch dll name count
    mkdir -p "${variant_dir}/i386-windows" "${variant_dir}/x86_64-windows"
    for arch in i386-windows x86_64-windows; do
        count=0
        for dll_dir in "${targets[@]}"; do
            name="$(basename "$(dirname "${dll_dir}")")"
            dll="${wine_build}/dlls/${name}/${arch}/${name}.dll"
            test -s "${dll}"
            cp -f "${dll}" "${variant_dir}/${arch}/${name}.dll"
            if [ "${arch}" = i386-windows ]; then
                i686-w64-mingw32-strip --strip-debug "${variant_dir}/${arch}/${name}.dll"
            else
                x86_64-w64-mingw32-strip --strip-debug "${variant_dir}/${arch}/${name}.dll"
            fi
            count=$((count + 1))
        done
        if [ "${count}" -ne 35 ]; then
            echo "ERROR: Only ${count} FAudio ${arch} DLLs built" >&2
            exit 1
        fi
    done

    {
        printf 'faudio=%s\nwine=%s\nvariant=%s\noverride=native\n' "${faudio_tag}" "${wine_tag}" "${variant}"
        printf 'faudio_revision=%s\nwine_revision=%s\nrecipe=%s\n' "${faudio_revision}" "${WINE_REVISION}" "${RECIPE}"
        printf 'backend=Win32/WASAPI\nlinkage=static\nwma=enabled\n'
        printf 'compiler_x86=%s\n' "$(i686-w64-mingw32-gcc -dumpfullversion)"
        printf 'compiler_x64=%s\n' "$(x86_64-w64-mingw32-gcc -dumpfullversion)"
        printf 'faudio_source_sha256=%s\n' "$(sha256sum "${faudio_archive}" | cut -d' ' -f1)"
        printf 'wine_source_sha256=%s\n' "$(sha256sum "${wine_archive}" | cut -d' ' -f1)"
    } > "${variant_dir}/version.txt"
    cp "${PATCH_DIR}/0003-audio-ordinary-pe.patch" "${variant_dir}/"
    cp "${PATCH_DIR}/0007-reverb-parameter-trace.patch" "${variant_dir}/"
    if [ "${variant}" = latest ]; then
        cp "${PATCH_DIR}"/000{1,2,4,5,6,8}-*.patch "${variant_dir}/"
    fi
    (cd "${variant_dir}" && sha256sum *.patch) > "${variant_dir}/patches.sha256"
    python3 "${SCRIPT_DIR}/verify-faudio.py" "${variant_dir}"
    (cd "${variant_dir}" && find i386-windows x86_64-windows -type f -name '*.dll' \
        | LC_ALL=C sort | xargs sha256sum) > "${variant_dir}/sha256sums.txt"
    cp -f "${faudio_source}/LICENSE" "${OUT_DIR}/FAudio-LICENSE"
    cp -f "${wine_source}/COPYING.LIB" "${OUT_DIR}/Wine-COPYING.LIB"
}

build_variant safe "${SAFE_FAUDIO}"
build_variant latest "${LATEST_FAUDIO}"
mkdir -p "${OUT_DIR}/tests"
for compiler in i686 x86_64; do
    "${compiler}-w64-mingw32-g++" -O2 -static -static-libgcc -static-libstdc++ \
        "${SCRIPT_DIR}/faudio-smoke.cpp" -lole32 -o "${OUT_DIR}/tests/${compiler}-smoke.exe"
done
echo "Bundled FAudio ${SAFE_FAUDIO} (baseline) and ${LATEST_FAUDIO} (patched candidate)."
