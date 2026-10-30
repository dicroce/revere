#!/bin/bash
#
# Builds a .deb of Revere from an already-built tree (e.g. the arm64 /
# Raspberry Pi CI build on Debian bookworm).
#
# Usage: ./build_deb.sh
#
# Installs to /opt/revere. Runtime dependencies come from dpkg-shlibdeps, so
# build on the oldest Debian release the package should install on. The
# package does not start a service: after installing,
#   sudo /opt/revere/revere --install-service
# runs Revere headless on boot. Upgrades restart that service; removal
# uninstalls it (keeping /var/lib/revere).
#
# The cloud system plugin is not packaged (it ships as the cloud .run).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
# All paths/version are overridable via env (for CI) and fall back to the
# local-dev defaults when unset.
BUILD_DIR="${REVERE_BUILD_DIR:-$PROJECT_DIR/build}"
OUTPUT_DIR="${REVERE_RELEASES_DIR:-$BUILD_DIR}"

# Same VERSION handling as create_cloud_plugin_installer.sh, so the .deb name
# matches the release's other assets.
if [ -z "${VERSION:-}" ]; then
    if VERSION="$(git -C "$PROJECT_DIR" describe --tags --exact-match 2>/dev/null)"; then
        :
    else
        VERSION="$(git -C "$PROJECT_DIR" rev-parse --short HEAD)"
    fi
fi
[[ "$VERSION" != v* ]] && VERSION="v${VERSION}"

# Debian versions must start with a digit: v0.0.31 -> 0.0.31, a hash build
# (vabc1234, v9c11372) -> 0.0.0~gabc1234, which sorts below any release. A
# release has dots; a hash, even one starting with a digit, has none.
if [[ "$VERSION" =~ ^v[0-9]+(\.[0-9]+)+$ ]]; then
    DEB_VERSION="${VERSION#v}"
else
    DEB_VERSION="0.0.0~g${VERSION#v}"
fi

ARCH="$(dpkg --print-architecture)"
mkdir -p "$OUTPUT_DIR"
OUTPUT_FILE="$OUTPUT_DIR/revere-${VERSION}-linux-${ARCH}.deb"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Error: no build found in $BUILD_DIR. Build revere first."
    exit 1
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
PKG_DIR="$WORK_DIR/pkg"

echo "Creating .deb for version: $VERSION ($DEB_VERSION, $ARCH)"
echo "Output: $OUTPUT_FILE"

# The install lays the app out under <prefix>/revere; anything else it
# installs (dependency headers, static libs) isn't needed at runtime.
cmake --install "$BUILD_DIR" --prefix "$WORK_DIR/install" > "$WORK_DIR/install.log" \
    || { cat "$WORK_DIR/install.log"; exit 1; }
mkdir -p "$PKG_DIR/opt"
mv "$WORK_DIR/install/revere" "$PKG_DIR/opt/revere"
rm -rf "$PKG_DIR/opt/revere/system_plugins"
find "$PKG_DIR/opt/revere" -name '*.a' -delete

# Without it Revere still records, but finds no objects (tier 1 and 3 run YOLO).
if [ ! -f "$PKG_DIR/opt/revere/motion_plugins/yolov8_motion_plugin.so" ]; then
    echo "Warning: no yolov8_motion_plugin.so in the build; this package has no object detection."
    echo "         Configure with NCNN_TOP_DIR set (see docs/BUILDING.md) to build it."
fi

mkdir -p "$PKG_DIR/usr/share/applications" "$PKG_DIR/usr/share/icons/hicolor/256x256/apps"
cp "$PROJECT_DIR/apps/revere/R.png" "$PKG_DIR/usr/share/icons/hicolor/256x256/apps/revere.png"
cp "$PROJECT_DIR/apps/vision/V.png" "$PKG_DIR/usr/share/icons/hicolor/256x256/apps/vision.png"
cat > "$PKG_DIR/usr/share/applications/revere.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Revere
Exec=/opt/revere/revere
Icon=revere
Categories=AudioVideo;Video;
Terminal=false
EOF
cat > "$PKG_DIR/usr/share/applications/vision.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Vision
Comment=Watch Revere's cameras, live and recorded
Exec=/opt/revere/vision
Icon=vision
Categories=AudioVideo;Video;
Terminal=false
EOF

# Runtime dependencies of every ELF file in the package. Our own libraries
# are found in the package itself (-l), and have no shlibs info to add. SDL2
# is built and shipped with revere, so a system SDL2 found on the build
# machine must not become a dependency (-x).
mapfile -t ELF_FILES < <(find "$PKG_DIR/opt/revere" -type f -exec sh -c 'head -c 4 "$1" | grep -q "ELF"' _ {} \; -print)
LIB_DIRS=()
while IFS= read -r d; do
    LIB_DIRS+=( "-l$d" )
done < <(printf '%s\n' "${ELF_FILES[@]}" | xargs -n1 dirname | sort -u)

mkdir -p "$WORK_DIR/shlibdeps/debian"
cat > "$WORK_DIR/shlibdeps/debian/control" << 'EOF'
Source: revere

Package: revere
Architecture: any
EOF
DEPENDS="$(cd "$WORK_DIR/shlibdeps" && dpkg-shlibdeps -O --ignore-missing-info -xlibsdl2-2.0-0 "${LIB_DIRS[@]}" "${ELF_FILES[@]}" 2> "$WORK_DIR/shlibdeps.log" \
    | sed -n 's/^shlibs:Depends=//p')"
if [ -z "$DEPENDS" ]; then
    cat "$WORK_DIR/shlibdeps.log"
    echo "Error: dpkg-shlibdeps found no dependencies"
    exit 1
fi

mkdir -p "$PKG_DIR/DEBIAN"
cat > "$PKG_DIR/DEBIAN/control" << EOF
Package: revere
Version: $DEB_VERSION
Architecture: $ARCH
Maintainer: Tony Di Croce <dicroce@gmail.com>
Installed-Size: $(du -sk "$PKG_DIR" | cut -f1)
Depends: $DEPENDS
Section: video
Priority: optional
Homepage: https://github.com/dicroce/revere
Description: Revere video surveillance system
 Records and analyzes IP cameras (ONVIF, RTSP), with a desktop app and a
 web UI. Run headless on boot with: sudo /opt/revere/revere --install-service
EOF

# Upgrade: a service installed with --install-service runs /opt/revere/revere,
# so restart it onto the new binary.
cat > "$PKG_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    # A service installed before Revere kept the computer awake needs the
    # polkit rule too (--install-service writes it for new ones).
    if [ -f /etc/systemd/system/revere.service ]; then
        /opt/revere/revere --install-keep-awake-rule > /dev/null || true
    fi
    if [ -d /run/systemd/system ] && systemctl is-active --quiet revere.service; then
        systemctl restart revere.service || true
    elif [ ! -f /etc/systemd/system/revere.service ]; then
        echo "To run Revere headless on boot: sudo /opt/revere/revere --install-service"
    fi
fi
exit 0
EOF

# Removal (not upgrade): take the service down with the binary it runs.
cat > "$PKG_DIR/DEBIAN/prerm" << 'EOF'
#!/bin/sh
set -e
if [ "$1" = "remove" ] && [ -f /etc/systemd/system/revere.service ]; then
    /opt/revere/revere --uninstall-service || true
fi
exit 0
EOF
chmod 755 "$PKG_DIR/DEBIAN/postinst" "$PKG_DIR/DEBIAN/prerm"

dpkg-deb --root-owner-group --build "$PKG_DIR" "$OUTPUT_FILE"

echo ""
echo "Package created: $OUTPUT_FILE"
echo "Size: $(du -h "$OUTPUT_FILE" | cut -f1)"
