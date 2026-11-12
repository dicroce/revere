#!/bin/bash
#
# Creates the self-extracting Revere Cloud installer for macOS, a .command that
# installs the plugin into ~/Library/Application Support/Revere/revere/system_plugins.
#
# Environment:
#   PLUGIN_PATH   the plugin dylib, as it will be installed (after install_name_tool
#                 and codesign). Its signed manifest and signature (<plugin>.json,
#                 <plugin>.json.sig) go in too when they sit beside it: release
#                 builds of revere load only a plugin whose manifest checks out.
#   PLUGIN_ARCH   arm64 or x86_64: the installer refuses to run on the other
#   ASSET         macos or macos-intel: the asset name's platform part
#   VERSION       v0.0.41
#   OUTPUT_DIR    where revere-<VERSION>-<ASSET>-cloud.command is written
#
# It needs only bash and tar, so it runs on any machine, not only a Mac.

set -euo pipefail

: "${PLUGIN_PATH:?}" "${PLUGIN_ARCH:?}" "${ASSET:?}" "${VERSION:?}" "${OUTPUT_DIR:?}"
[[ "$VERSION" != v* ]] && VERSION="v${VERSION}"

if [ ! -f "$PLUGIN_PATH" ]; then
    echo "Error: plugin not found at $PLUGIN_PATH"
    exit 1
fi

PLUGIN_NAME="revere_cloud_system_plugin.dylib"
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

mkdir -p "$OUTPUT_DIR"
out="$OUTPUT_DIR/revere-${VERSION}-${ASSET}-cloud.command"
echo "Creating $out with: $PAYLOAD_FILES"

echo '#!/bin/bash' > "$out"
# The plugin only loads into a Revere of its own architecture.
echo "PLUGIN_ARCH=\"$PLUGIN_ARCH\"" >> "$out"
cat >> "$out" <<'HDR'
set -e
PLUGIN_DIR="$HOME/Library/Application Support/Revere/revere/system_plugins"
PLUGIN_NAME="revere_cloud_system_plugin.dylib"
echo "Revere Cloud Plugin Installer"; echo "=============================="; echo ""
if [ "$(uname -m)" != "$PLUGIN_ARCH" ]; then
  if [ "$PLUGIN_ARCH" = "x86_64" ]; then
    echo "Error: this installer is for Intel Macs. This Mac has Apple silicon: use revere-<version>-macos-cloud.command."
  else
    echo "Error: this installer is for Macs with Apple silicon. On an Intel Mac use revere-<version>-macos-intel-cloud.command."
  fi
  read -p "Press Enter to exit..."; exit 1
fi
if [ ! -d "$HOME/Library/Application Support/Revere/revere" ]; then
  echo "Error: Revere does not appear to be installed."
  echo "Please run Revere at least once before installing the plugin."
  read -p "Press Enter to exit..."; exit 1
fi
mkdir -p "$PLUGIN_DIR"
# An older install's manifest and signature describe its plugin, not this one.
rm -f "$PLUGIN_DIR/$PLUGIN_NAME.json" "$PLUGIN_DIR/$PLUGIN_NAME.json.sig"
ARCHIVE_START=$(awk '/^__PAYLOAD__$/{print NR + 1; exit 0; }' "$0")
tail -n +"$ARCHIVE_START" "$0" | tar -xzf - -C "$PLUGIN_DIR"
chmod 755 "$PLUGIN_DIR/$PLUGIN_NAME"
echo ""; echo "Installation complete! Restart Revere to load the cloud plugin."
read -p "Press Enter to exit..."; exit 0
__PAYLOAD__
HDR
tar -czf - -C "$PAYLOAD_DIR" $PAYLOAD_FILES >> "$out"
chmod +x "$out"
ls -la "$out"
