#!/bin/bash
#
# Creates a self-extracting installer for the Revere Cloud Plugin (Linux)
#
# Usage: ./create_cloud_plugin_installer.sh
#
# Expects the plugin to be built at: build/system_plugins/revere_cloud_system_plugin.so

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
case "$(uname -m)" in
    x86_64)  ARCH_SUFFIX="" ;;
    aarch64) ARCH_SUFFIX="-arm64" ;;
    *)       ARCH_SUFFIX="-$(uname -m)" ;;
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

# Create the installer
cat > "$OUTPUT_FILE" << 'INSTALLER_HEADER'
#!/bin/bash
# Revere Cloud Plugin Installer
# This is a self-extracting installer - the .so is appended after the __PAYLOAD__ marker
#
# Usage: ./revere-<version>-linux-cloud.run [data-dir]
#   data-dir defaults to ~/Documents/revere/revere (the desktop app's). For
#   Revere running as a service: sudo ./revere-<version>-linux-cloud.run /var/lib/revere

set -e

DATA_DIR="${1:-$HOME/Documents/revere/revere}"
PLUGIN_DIR="$DATA_DIR/system_plugins"
PLUGIN_NAME="revere_cloud_system_plugin.so"

echo "Revere Cloud Plugin Installer"
echo "=============================="
echo ""

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
tail -n +"$ARCHIVE_START" "$0" > "$PLUGIN_DIR/$PLUGIN_NAME"
chmod 755 "$PLUGIN_DIR/$PLUGIN_NAME"
# Installed as root into a service's data dir: give it to the service's user.
chown --reference="$DATA_DIR" "$PLUGIN_DIR" "$PLUGIN_DIR/$PLUGIN_NAME" 2>/dev/null || true

echo ""
echo "Installation complete!"
echo "Restart Revere to load the cloud plugin."

exit 0

__PAYLOAD__
INSTALLER_HEADER

# Append the plugin binary
cat "$PLUGIN_PATH" >> "$OUTPUT_FILE"

# Make executable
chmod +x "$OUTPUT_FILE"

echo ""
echo "Installer created: $OUTPUT_FILE"
echo "Size: $(du -h "$OUTPUT_FILE" | cut -f1)"
