#!/bin/bash
#
# Plain local build of Revere. Two steps — build as a normal user, install as root:
#
#   ./scripts/linux/build.sh [extra cmake args...]   Configure + build. Must NOT
#                                                     be run as root.
#   sudo ./scripts/linux/build.sh --install          Install the built tree to
#                                                     /usr/local/revere. Run as root.
#
# Typical use:
#   ./scripts/linux/build.sh
#   sudo ./scripts/linux/build.sh --install
#
# Extra args in build mode are passed through to the configure step, e.g.
#   ./scripts/linux/build.sh -DEXTERNAL_PLUGIN_REPOS=/path/to/revere_cloud
#
# Set NCNN_TOP_DIR in the environment first if you want the AI detection plugins.
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

INSTALL=0
for a in "$@"; do
    [ "$a" = "--install" ] && INSTALL=1
done

# ---- install mode (root) ---------------------------------------------------
if [ "$INSTALL" -eq 1 ]; then
    if [ "$(id -u)" -ne 0 ]; then
        echo "Error: --install must be run as root, e.g.  sudo $0 --install" >&2
        exit 1
    fi
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        echo "Error: no build found in $BUILD_DIR." >&2
        echo "       Run './scripts/linux/build.sh' (as a normal user) first." >&2
        exit 1
    fi
    cmake --install "$BUILD_DIR"
    echo ""
    echo "Installed to /usr/local/revere."
    echo "  Desktop:           /usr/local/revere/revere"
    echo "  Headless service:  sudo /usr/local/revere/revere --install-service"
    exit 0
fi

# ---- build mode (non-root) -------------------------------------------------
if [ "$(id -u)" -eq 0 ]; then
    echo "Error: build as a normal user, not root (building as root leaves" >&2
    echo "       root-owned files in the build tree)." >&2
    echo "       To install after building:  sudo $0 --install" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE=Release "$@" ..
cmake --build . -j"$(nproc)"

echo ""
echo "Build complete. To install:  sudo $0 --install"
