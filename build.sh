#!/usr/bin/env bash
set -euo pipefail

# Build Fluorine Manager using Docker.
#
# Usage:
#   ./build.sh              # Build AppImage + staging dir
#   ./build.sh shell        # Drop into the build container for debugging
#
# Prerequisites: Docker or Podman

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Auto-detect container runtime
if command -v podman >/dev/null 2>&1; then
    DOCKER=podman
elif command -v docker >/dev/null 2>&1; then
    DOCKER=docker
else
    echo "ERROR: Neither podman nor docker found in PATH"
    exit 1
fi
IMAGE_NAME="fluorine-builder"
CONTAINER_NAME="fluorine-build-$$"

cd "${SCRIPT_DIR}"

# Build the Docker image if it doesn't exist or Dockerfile changed
echo "=== Ensuring build image is up to date ==="
${DOCKER} build -t "${IMAGE_NAME}" docker/

if [ "${1:-}" = "shell" ]; then
    echo "=== Dropping into build container shell ==="
    exec ${DOCKER} run --rm -it \
        -v "${SCRIPT_DIR}:/src:rw" \
        -w /src \
        --device /dev/fuse \
        --cap-add SYS_ADMIN \
        "${IMAGE_NAME}" \
        bash
fi

echo "=== Starting build ==="
${DOCKER} run --rm \
    -v "${SCRIPT_DIR}:/src:rw" \
    -w /src \
    --device /dev/fuse \
    --cap-add SYS_ADMIN \
    --name "${CONTAINER_NAME}" \
    "${IMAGE_NAME}" \
    bash /src/docker/build-inner.sh

echo ""
echo "=== Done ==="
if ls build/*.AppImage >/dev/null 2>&1; then
    echo "AppImage:  $(ls build/*.AppImage)"
fi
echo "Staging:   build/staging/"
