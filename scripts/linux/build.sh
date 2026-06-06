#!/bin/bash
#
# Plain local build of Revere: configure (Release), compile, and — by default —
# install to /usr/local/revere. This is the developer / "I just want to run it"
# build. For the distributable snap use build_snap.sh instead.
#
# Usage: ./build.sh [--no-install] [extra cmake args...]
#   --no-install        Configure and build only; skip the install step.
#   <extra cmake args>  Passed through to the configure step, e.g.
#                       -DEXTERNAL_PLUGIN_REPOS=/path/to/revere_cloud
#
# Set NCNN_TOP_DIR in the environment first if you want the AI detection plugins.
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

DO_INSTALL=1
CMAKE_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --no-install) DO_INSTALL=0 ;;
        *)            CMAKE_ARGS+=("$arg") ;;
    esac
done

BUILD_DIR="$PROJECT_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -DCMAKE_BUILD_TYPE=Release "${CMAKE_ARGS[@]}" ..
cmake --build . -j"$(nproc)"

echo ""
echo "Build complete."

if [ "$DO_INSTALL" -eq 1 ]; then
    echo "Installing to /usr/local/revere (requires sudo)..."
    sudo cmake --install .
    echo ""
    echo "Installed to /usr/local/revere."
    echo "  Desktop:  /usr/local/revere/revere"
    echo "  Headless service:  sudo /usr/local/revere/revere --install-service"
else
    echo "Skipping install (--no-install)."
    echo "To install later:  sudo cmake --install \"$BUILD_DIR\""
fi
