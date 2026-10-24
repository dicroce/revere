#!/bin/bash
#
# Builds the Revere snap and renames the output to the
# revere-v<version>-linux-app.snap convention.
#
# Usage: ./build_snap.sh [extra snapcraft args]
#   e.g. ./build_snap.sh --use-lxd

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$PROJECT_DIR"

snapcraft "$@"

SNAP_FILE="$(ls -t revere_*_amd64.snap 2>/dev/null | head -n1 || true)"
if [ -z "$SNAP_FILE" ]; then
    echo "Error: no revere_*_amd64.snap produced by snapcraft" >&2
    exit 1
fi

if VERSION="$(git describe --tags --exact-match 2>/dev/null)"; then
    :
else
    VERSION="$(git rev-parse --short HEAD)"
fi
[[ "$VERSION" != v* ]] && VERSION="v${VERSION}"
OUTPUT_FILE="revere-${VERSION}-linux-app.snap"

mv -f "$SNAP_FILE" "$OUTPUT_FILE"

echo ""
echo "Snap created: $PROJECT_DIR/$OUTPUT_FILE"
echo "Size: $(du -h "$OUTPUT_FILE" | cut -f1)"

BUILD_DIR="$PROJECT_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release -DEXTERNAL_PLUGIN_REPOS=../../revere_cloud ..
cmake --build . -j"$(nproc)"

# The cloud plugin installer, the same one the release workflow builds (it
# installs into the Snap's own plugin folder when Revere is a Snap).
VERSION="$VERSION" \
REVERE_BUILD_DIR="$BUILD_DIR" \
REVERE_RELEASES_DIR="$BUILD_DIR" \
bash "$SCRIPT_DIR/create_cloud_plugin_installer.sh"
