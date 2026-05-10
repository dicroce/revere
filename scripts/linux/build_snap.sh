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

PLUGIN_PATH="$BUILD_DIR/system_plugins/revere_cloud_system_plugin.so"
INSTALLER_FILE="$BUILD_DIR/revere-${VERSION}-linux-cloud.run"

if [ ! -f "$PLUGIN_PATH" ]; then
    echo "Error: Plugin not found at $PLUGIN_PATH" >&2
    exit 1
fi

echo ""
echo "Creating cloud plugin installer for version: $VERSION"
echo "Plugin: $PLUGIN_PATH"
echo "Output: $INSTALLER_FILE"

cat > "$INSTALLER_FILE" << 'INSTALLER_HEADER'
#!/bin/bash
# Revere Cloud Plugin Installer
# This is a self-extracting installer - the .so is appended after the __PAYLOAD__ marker

set -e

PLUGIN_DIR="$HOME/Documents/revere/revere/system_plugins"
PLUGIN_NAME="revere_cloud_system_plugin.so"

echo "Revere Cloud Plugin Installer"
echo "=============================="
echo ""

if [ ! -d "$HOME/Documents/revere/revere" ]; then
    echo "Error: Revere does not appear to be installed."
    echo "Please run Revere at least once before installing the plugin."
    exit 1
fi

mkdir -p "$PLUGIN_DIR"

ARCHIVE_START=$(awk '/^__PAYLOAD__$/{print NR + 1; exit 0; }' "$0")

echo "Installing plugin to: $PLUGIN_DIR/$PLUGIN_NAME"
tail -n +"$ARCHIVE_START" "$0" > "$PLUGIN_DIR/$PLUGIN_NAME"
chmod 755 "$PLUGIN_DIR/$PLUGIN_NAME"

echo ""
echo "Installation complete!"
echo "Restart Revere to load the cloud plugin."

exit 0

__PAYLOAD__
INSTALLER_HEADER

cat "$PLUGIN_PATH" >> "$INSTALLER_FILE"
chmod +x "$INSTALLER_FILE"

echo ""
echo "Installer created: $INSTALLER_FILE"
echo "Size: $(du -h "$INSTALLER_FILE" | cut -f1)"
