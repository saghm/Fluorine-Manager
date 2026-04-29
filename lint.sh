#!/usr/bin/env bash
#
# Run clang-tidy over source translation units we maintain, inside the build container so
# the toolchain (gcc 15, libstdc++, Qt 6.10 headers, etc.) matches what
# generated build/compile_commands.json.
#
# Usage:
#   ./lint.sh                  # lint all our source files (parallel)
#   ./lint.sh path/to/file.cpp # lint a single file (relative to project root)
#   ./lint.sh --fix            # apply clang-tidy's auto-fixes in place
#
# Requires:
#   - build/compile_commands.json (built by `./build.sh tarball`)
#   - The fluorine-builder Docker image with clang-tidy installed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}"

if [ ! -f build/compile_commands.json ]; then
    echo "ERROR: build/compile_commands.json not found."
    echo "Run './build.sh tarball' first to generate it."
    exit 1
fi

if command -v podman >/dev/null 2>&1; then
    DOCKER=podman
elif command -v docker >/dev/null 2>&1; then
    DOCKER=docker
else
    echo "ERROR: neither podman nor docker found in PATH"
    exit 1
fi

IMAGE_NAME="fluorine-builder"

if ! ${DOCKER} images --format '{{.Repository}}' 2>/dev/null \
        | grep -qE "(^|/)${IMAGE_NAME}$"; then
    echo "ERROR: ${IMAGE_NAME} image not found. Run './build.sh tarball' first."
    exit 1
fi

if ! ${DOCKER} run --rm "${IMAGE_NAME}" which clang-tidy >/dev/null 2>&1; then
    echo "WARNING: clang-tidy not in ${IMAGE_NAME}. Rebuilding image..."
    ${DOCKER} build -t "${IMAGE_NAME}" docker/
fi

FIX_FLAG=()
SINGLE_FILE=""
for arg in "$@"; do
    case "$arg" in
        --fix) FIX_FLAG+=(--fix) ;;
        --*)   echo "unknown flag: $arg"; exit 1 ;;
        *)     SINGLE_FILE="$arg" ;;
    esac
done

collect_files() {
    # Headers are checked through their owning translation units. Running
    # clang-tidy directly on every header produces duplicate diagnostics and
    # false positives for Qt-generated include paths.
    #
    # browserview / browserdialog depend on QtWebEngine which is Windows-only
    # in this fork (CMake removes them from ORGANIZER_SOURCES on Linux), so
    # clang-tidy can't parse their headers. Skip them.
    {
        find src/src -type f -name '*.cpp'
        find libs/skse_log_redirector -type f -name '*.cpp'
    } | grep -v -E '(/moc_|/ui_|/qrc_|_autogen/|/build/|browserview|browserdialog)'
}

if [ -n "${SINGLE_FILE}" ]; then
    REL_FILES=("${SINGLE_FILE#./}")
else
    mapfile -t REL_FILES < <(collect_files | sort)
fi

if [ "${#REL_FILES[@]}" -eq 0 ]; then
    echo "no files to lint"
    exit 0
fi

CONTAINER_PATHS=()
for f in "${REL_FILES[@]}"; do
    CONTAINER_PATHS+=("/src/${f}")
done

JOBS="${BUILD_JOBS:-4}"

echo "Linting ${#CONTAINER_PATHS[@]} files in ${IMAGE_NAME} (${JOBS} jobs)..."

# Container builds with GCC, which uses -mno-direct-extern-access — clang in
# the same container doesn't recognise that flag, so strip it from the
# compile DB before running clang-tidy. Copy to a tmp DB so we don't disturb
# ninja's incremental build.
printf '%s\n' "${CONTAINER_PATHS[@]}" | \
    ${DOCKER} run --rm -i \
        -v "${SCRIPT_DIR}:/src:rw" \
        -w /src \
        "${IMAGE_NAME}" \
        bash -c '
            set -e
            TIDY_DIR=$(mktemp -d)
            trap "rm -rf $TIDY_DIR" EXIT
            sed "s| -mno-direct-extern-access||g" /src/build/compile_commands.json \
                > "$TIDY_DIR/compile_commands.json"
            # stdbuf -oL forces line-buffered stdout so output flushes through
            # the podman pipe instead of being held until container exit.
            xargs -P '"${JOBS}"' -I{} stdbuf -oL clang-tidy '"${FIX_FLAG[*]:-}"' \
                -p "$TIDY_DIR" \
                --extra-arg=-Wno-ignored-gch \
                --exclude-header-filter="(^|/)(build/|.*_autogen/|ui_.*\\.h$)" \
                --quiet {}
        '
