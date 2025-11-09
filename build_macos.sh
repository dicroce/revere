#!/bin/bash

# Revere macOS Build Script
# This script automates the build process for Revere on macOS

set -e  # Exit on error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_TYPE=${1:-Release}
CLEAN_BUILD=${2:-false}

echo "========================================"
echo "Revere macOS Build Script"
echo "========================================"
echo "Build Type: $BUILD_TYPE"
echo "Clean Build: $CLEAN_BUILD"
echo ""

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check prerequisites
echo "Checking prerequisites..."

if ! command_exists brew; then
    echo "Error: Homebrew is not installed."
    echo "Please install Homebrew from https://brew.sh"
    exit 1
fi

if ! command_exists cmake; then
    echo "Error: CMake is not installed."
    echo "Installing CMake..."
    brew install cmake
fi

if ! command_exists pkg-config; then
    echo "Error: pkg-config is not installed."
    echo "Installing pkg-config..."
    brew install pkg-config
fi

# Check for required dependencies
echo "Checking dependencies..."

check_brew_package() {
    if brew list --versions "$1" > /dev/null; then
        echo "✓ $1 is installed"
    else
        echo "✗ $1 is not installed"
        return 1
    fi
}

MISSING_DEPS=()

# Check each required dependency
for dep in opencv gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad ffmpeg glfw sqlite mbedtls pugixml; do
    if ! check_brew_package "$dep"; then
        MISSING_DEPS+=("$dep")
    fi
done

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo ""
    echo "Missing dependencies: ${MISSING_DEPS[*]}"
    read -p "Do you want to install missing dependencies? (y/n) " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Installing missing dependencies..."
        brew install "${MISSING_DEPS[@]}"
    else
        echo "Please install missing dependencies manually and run this script again."
        exit 1
    fi
fi

# Set up environment variables for pkg-config
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"  # For Apple Silicon Macs

# Check for NCNN (optional)
if [ -n "$NCNN_TOP_DIR" ] && [ -d "$NCNN_TOP_DIR" ]; then
    echo "✓ NCNN found at: $NCNN_TOP_DIR"
else
    echo "ℹ NCNN not configured (AI motion detection will not be available)"
    echo "  To enable, build NCNN and set NCNN_TOP_DIR environment variable"
fi

# Clean build if requested
if [ "$CLEAN_BUILD" = "true" ] || [ "$CLEAN_BUILD" = "clean" ]; then
    echo ""
    echo "Cleaning previous build..."
    rm -rf "$SCRIPT_DIR/build"
fi

# Create build directory
echo ""
echo "Creating build directory..."
mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"

# Configure with CMake
echo ""
echo "Configuring with CMake..."
cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      ..

if [ $? -ne 0 ]; then
    echo "Error: CMake configuration failed"
    exit 1
fi

# Build
echo ""
echo "Building Revere..."
NUM_CORES=$(sysctl -n hw.ncpu)
echo "Using $NUM_CORES CPU cores for build"

make -j"$NUM_CORES"

if [ $? -ne 0 ]; then
    echo "Error: Build failed"
    exit 1
fi

echo ""
echo "========================================"
echo "Build completed successfully!"
echo "========================================"
echo ""
echo "Executables built:"
echo "  - Main app: $SCRIPT_DIR/build/apps/revere/revere"
echo "  - Viewer:   $SCRIPT_DIR/build/apps/vision/vision"
echo ""
echo "To run Revere:"
echo "  1. Start the main service: ./build/apps/revere/revere"
echo "  2. Start the viewer:        ./build/apps/vision/vision"
echo ""
echo "To install system-wide:"
echo "  sudo make -C build install"
echo ""