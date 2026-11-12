#!/bin/bash
#
# Creates a self-extracting installer for the Revere Cloud Plugin (Linux)
#
# Usage: ./create_cloud_plugin_installer.sh
#
# Expects the plugin to be built at: build/system_plugins/revere_cloud_system_plugin.so
#
# The plugin's signed manifest and its signature (<plugin>.json, <plugin>.json.sig,
# from a release build's sign job) go in too when they sit beside it: release
# builds of revere load only a plugin whose manifest checks out
# (libs/r_vss/r_plugin_manifest.h). The payload is a gzipped tar of those files.
#
# REVERE_CLOUD_ARCH (x86_64 or arm64) names the installer's architecture when it's
# packed on another machine; it defaults to this one's.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
# All paths/version are overridable via env (for CI) and fall back to the
# historical local-dev defaults when unset.
BUILD_DIR="${REVERE_BUILD_DIR:-$PROJECT_DIR/build}"
PLUGIN_PATH="${REVERE_CLOUD_PLUGIN_PATH:-$BUILD_DIR/system_plugins/revere_cloud_system_plugin.so}"
OUTPUT_DIR="${REVERE_RELEASES_DIR:-$BUILD_DIR}"

# Honor a caller-supplied VERSION (CI passes the release version so the .run
# name matches the in-app download URL exactly); otherwise derive from git.
if [ -z "${VERSION:-}" ]; then
    if VERSION="$(git -C "$PROJECT_DIR" describe --tags --exact-match 2>/dev/null)"; then
        :
    else
        VERSION="$(git -C "$PROJECT_DIR" rev-parse --short HEAD)"
    fi
fi
[[ "$VERSION" != v* ]] && VERSION="v${VERSION}"
mkdir -p "$OUTPUT_DIR"
# x86_64 keeps the original name; other architectures name theirs, matching
# the in-app download URL (_revere_cloud_download_url in main.cpp).
case "${REVERE_CLOUD_ARCH:-$(uname -m)}" in
    x86_64)        ARCH_SUFFIX="" ;;
    aarch64|arm64) ARCH_SUFFIX="-arm64" ;;
    *)             ARCH_SUFFIX="-${REVERE_CLOUD_ARCH:-$(uname -m)}" ;;
esac
OUTPUT_FILE="$OUTPUT_DIR/revere-${VERSION}-linux${ARCH_SUFFIX}-cloud.run"

# Check plugin exists
if [ ! -f "$PLUGIN_PATH" ]; then
    echo "Error: Plugin not found at $PLUGIN_PATH"
    echo "Build revere with -DEXTERNAL_PLUGIN_REPOS=/path/to/revere_cloud first"
    exit 1
fi

echo "Creating installer for version: $VERSION"
echo "Plugin: $PLUGIN_PATH"
echo "Output: $OUTPUT_FILE"

# The payload: the plugin under its installed name, and its manifest and
# signature when they're there.
PLUGIN_NAME="revere_cloud_system_plugin.so"
PAYLOAD_DIR="$(mktemp -d)"
trap 'rm -rf "$PAYLOAD_DIR"' EXIT
cp "$PLUGIN_PATH" "$PAYLOAD_DIR/$PLUGIN_NAME"
PAYLOAD_FILES="$PLUGIN_NAME"
for suffix in .json .json.sig; do
    if [ -f "$PLUGIN_PATH$suffix" ]; then
        cp "$PLUGIN_PATH$suffix" "$PAYLOAD_DIR/$PLUGIN_NAME$suffix"
        PAYLOAD_FILES="$PAYLOAD_FILES $PLUGIN_NAME$suffix"
    fi
done
echo "Payload: $PAYLOAD_FILES"

# Create the installer
cat > "$OUTPUT_FILE" << 'INSTALLER_HEADER'
#!/bin/bash
# Revere Cloud Plugin Installer
# This is a self-extracting installer - a gzipped tar of the .so and its signed
# manifest is appended after the __PAYLOAD__ marker
#
# Usage: ./revere-<version>-linux-cloud.run [data-dir]
#   With no data-dir it installs for the desktop app: into the Revere Snap's
#   own folder (~/snap/revere/common/system_plugins) when Revere is installed
#   as a Snap, else into ~/Documents/revere/revere. For Revere running as a
#   service: sudo ./revere-<version>-linux-cloud.run /var/lib/revere

set -e

PLUGIN_NAME="revere_cloud_system_plugin.so"
DESKTOP_DATA_DIR="$HOME/Documents/revere/revere"
SNAP_INSTALL=0

if [ -n "${1:-}" ]; then
    DATA_DIR="$1"
elif { command -v snap >/dev/null 2>&1 && snap list revere >/dev/null 2>&1; } ||
     { ! command -v snap >/dev/null 2>&1 && [ -d "$HOME/snap/revere" ]; }; then
    # snapd's own answer when it's on the PATH; a left-over ~/snap/revere
    # only counts when it isn't.
    # The Snap can read ~/Documents but may only load code from its own
    # folders under ~/snap/revere, so its plugins live there.
    SNAP_INSTALL=1
    DATA_DIR="$HOME/snap/revere/common"
else
    DATA_DIR="$DESKTOP_DATA_DIR"
fi
PLUGIN_DIR="$DATA_DIR/system_plugins"

echo "Revere Cloud Plugin Installer"
echo "=============================="
echo ""
if [ "$SNAP_INSTALL" -eq 1 ]; then
    echo "Revere is installed as a Snap: installing into the Snap's plugin folder."
    echo "(To install somewhere else, pass the data folder: $0 <data-dir>)"
    echo ""
fi

# Check if revere directory exists
if [ ! -d "$DATA_DIR" ]; then
    echo "Error: Revere does not appear to be installed (no $DATA_DIR)."
    echo "Please run Revere at least once before installing the plugin."
    exit 1
fi

# Create plugin directory if needed
mkdir -p "$PLUGIN_DIR"

# Extract payload (everything after __PAYLOAD__ marker)
ARCHIVE_START=$(awk '/^__PAYLOAD__$/{print NR + 1; exit 0; }' "$0")

echo "Installing plugin to: $PLUGIN_DIR/$PLUGIN_NAME"
# An older install's manifest and signature describe its plugin, not this one.
rm -f "$PLUGIN_DIR/$PLUGIN_NAME.json" "$PLUGIN_DIR/$PLUGIN_NAME.json.sig"
tail -n +"$ARCHIVE_START" "$0" | tar -xzf - -C "$PLUGIN_DIR" --no-same-owner
chmod 755 "$PLUGIN_DIR/$PLUGIN_NAME"
# Installed as root into a service's data dir: give it to the service's user.
for f in "$PLUGIN_DIR" "$PLUGIN_DIR/$PLUGIN_NAME" "$PLUGIN_DIR/$PLUGIN_NAME.json" "$PLUGIN_DIR/$PLUGIN_NAME.json.sig"; do
    [ -e "$f" ] && chown --reference="$DATA_DIR" "$f" 2>/dev/null || true
done

# A copy from an earlier installer, which the Snap can't load: say it can go.
if [ "$SNAP_INSTALL" -eq 1 ] && [ -f "$DESKTOP_DATA_DIR/system_plugins/$PLUGIN_NAME" ]; then
    echo ""
    echo "Note: $DESKTOP_DATA_DIR/system_plugins/$PLUGIN_NAME is an older copy"
    echo "the Snap can't load. You can delete it."
fi

echo ""
echo "Installation complete!"
echo "Restart Revere to load the cloud plugin."

exit 0

__PAYLOAD__
INSTALLER_HEADER

# Append the payload
tar -czf - -C "$PAYLOAD_DIR" $PAYLOAD_FILES >> "$OUTPUT_FILE"

# Make executable
chmod +x "$OUTPUT_FILE"

echo ""
echo "Installer created: $OUTPUT_FILE"
echo "Size: $(du -h "$OUTPUT_FILE" | cut -f1)"
