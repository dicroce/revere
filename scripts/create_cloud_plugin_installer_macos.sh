#!/bin/bash
#
# Creates a self-extracting installer for the Revere Cloud Plugin (macOS)
#
# Usage: ./create_cloud_plugin_installer_macos.sh
#
# Expects the plugin to be built at: build/_deps/revere_cloud-build/revere_cloud_system_plugin.dylib

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
PLUGIN_PATH="$BUILD_DIR/_deps/revere_cloud-build/revere_cloud_system_plugin.dylib"

VERSION="$(git -C "$PROJECT_DIR" describe --tags --always)"
OUTPUT_FILE="$BUILD_DIR/install-revere-cloud-plugin-${VERSION}-macos.command"

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
# Revere Cloud Plugin Installer for macOS
# This is a self-extracting installer - the .dylib is appended after the __PAYLOAD__ marker

set -e

PLUGIN_DIR="$HOME/Library/Application Support/Revere/revere/system_plugins"
PLUGIN_NAME="revere_cloud_system_plugin.dylib"

echo "Revere Cloud Plugin Installer"
echo "=============================="
echo ""

# Check if revere directory exists
if [ ! -d "$HOME/Library/Application Support/Revere/revere" ]; then
    echo "Error: Revere does not appear to be installed."
    echo "Please run Revere at least once before installing the plugin."
    echo ""
    read -p "Press Enter to exit..."
    exit 1
fi

# Create plugin directory if needed
mkdir -p "$PLUGIN_DIR"

# Extract payload (everything after __PAYLOAD__ marker)
ARCHIVE_START=$(awk '/^__PAYLOAD__$/{print NR + 1; exit 0; }' "$0")

echo "Installing plugin to: $PLUGIN_DIR/$PLUGIN_NAME"
tail -n +"$ARCHIVE_START" "$0" > "$PLUGIN_DIR/$PLUGIN_NAME"
chmod 755 "$PLUGIN_DIR/$PLUGIN_NAME"

echo ""
echo "Installation complete!"
echo "Restart Revere to load the cloud plugin."
echo ""
read -p "Press Enter to exit..."

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
echo ""
echo "Users can double-click this .command file to install the plugin."
