# Building Revere from Source

This guide covers building Revere on all supported platforms.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Building on Linux](#building-on-linux)
  - [Ubuntu/Debian](#ubuntudebian)
  - [Fedora/RHEL](#fedorarhel)
  - [Arch Linux](#arch-linux)
- [Building on macOS](#building-on-macos)
- [Building on Windows](#building-on-windows)
  - [Installing Dependencies](#installing-dependencies)
  - [Setting Environment Variables](#setting-environment-variables)
  - [Building with CMake](#building-with-cmake)
- [Build Options](#build-options)
- [Optional Dependencies](#optional-dependencies)
- [Running Tests](#running-tests)
- [Common Build Issues](#common-build-issues)

## Prerequisites

### All Platforms

- CMake 3.14 or higher
- C++17 compatible compiler
- Git
- OpenCV 4.x
- GStreamer 1.0
- FFmpeg 4.x, 5.x, or 6.x

### Platform-Specific Tools

**Linux:**
- GCC 9+ or Clang 10+
- pkg-config
- make or ninja
- Development packages for all dependencies

**macOS:**
- macOS 10.15 (Catalina) or newer
- Xcode Command Line Tools
- Homebrew package manager

**Windows:**
- Visual Studio 2019 or newer (VS 2022/MSVC v17 recommended)
- Windows SDK

## Building on Linux

### Ubuntu/Debian

#### Installing Dependencies

```bash
# System dependencies
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libuuid1 \
    libgtk-3-dev \
    libbz2-dev \
    uuid-dev \
    libayatana-appindicator3-dev

# OpenCV
sudo apt-get install -y \
    libopencv-dev \
    libopencv-contrib-dev

# GStreamer
sudo apt-get install -y \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-tools

# FFmpeg
sudo apt-get install -y \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    ffmpeg
```

#### Optional: NCNN for AI Motion Detection

NCNN enables AI-based person detection plugins (YOLOv8, MobileNet, PicoDet).

```bash
# Clone and build NCNN
mkdir $HOME/NCNN_INSTALL
git clone https://github.com/Tencent/ncnn.git
cd ncnn
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DNCNN_BUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=$HOME/NCNN_INSTALL ..
make -j$(nproc)
sudo make install
export NCNN_TOP_DIR=$HOME/NCNN_INSTALL

```

#### Building Revere

```bash
# Clone the repository
git clone [<repository-url>](https://github.com/dicroce/revere.git)
cd revere

# Create build directory
mkdir build
cd build

# Configure
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
make -j$(nproc)

# Install (optional)
sudo make install
```

The executables will be in `build/apps/revere/` and `build/apps/vision/`. make install will install them to /usr/loca/revere (it also should add an icon to your gui).

### Fedora/RHEL

TBD (should probably work)

### Arch Linux

TBD (should probably work)

## Building on macOS

For detailed macOS build instructions, see [BUILDING_MACOS.md](BUILDING_MACOS.md).

### Quick Start

```bash
# Install dependencies with Homebrew
brew install cmake pkg-config opencv gstreamer gst-plugins-base \
             gst-plugins-good gst-plugins-bad ffmpeg glfw sqlite \
             mbedtls pugixml

# Clone and build
git clone https://github.com/dicroce/revere.git
cd revere
./build_macos.sh

# Or manually:
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
```

## Building on Windows

### Installing Dependencies

#### Visual Studio

Download and install Visual Studio 2019 or newer from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/).
- Recommended: Visual Studio 2022 (MSVC v17)
- Required components: "Desktop development with C++"

#### GIT

- Installing GIT will give you GIT bash. I usually do everything but the finall install from a git bash command line.

#### CMake

Download from [cmake.org](https://cmake.org/download/) and install, or use Visual Studio's included CMake.

#### OpenCV

Download OpenCV 4.x prebuilt binaries from [opencv.org](https://opencv.org/releases/).
- Extract to a location like `C:\opencv`
- Set environment variable: `OPENCV_TOP_DIR=C:\opencv\build`
- Required structure: `OPENCV_TOP_DIR\x64\vc17\lib` (for VS 2022) or `vc16` (for VS 2019)
- The build system expects OpenCV 4.12.0 or compatible

#### GStreamer

Download GStreamer 1.0 MSVC runtime and development installers from [gstreamer.freedesktop.org](https://gstreamer.freedesktop.org/download/).
- Install both "runtime" and "development" packages
- Choose "Complete" installation to get all plugins
- Set environment variable: `GST_TOP_DIR=C:\gstreamer\1.0\msvc_x86_64`
- Add GStreamer bin directory to PATH: `C:\gstreamer\1.0\msvc_x86_64\bin`

#### FFmpeg

Download FFmpeg shared libraries from [ffmpeg.org](https://ffmpeg.org/download.html) or [gyan.dev](https://www.gyan.dev/ffmpeg/builds/).
- Extract to a location like `C:\ffmpeg`
- Set environment variable: `FFMPEG_TOP_DIR=C:\ffmpeg`
- Required DLLs: avcodec-62.dll, avformat-62.dll, avutil-60.dll, swscale-9.dll (versions may vary for FFmpeg 4.x/5.x)
- Ensure DLLs are in a directory added to PATH or copy them to the executable directory after build

#### Optional: NCNN

For AI motion detection plugins:
- Build NCNN from source or download prebuilt libraries
- Set environment variable: `NCNN_TOP_DIR=C:\path\to\ncnn`
- Structure should include `lib\ncnn.lib` (Release) or `lib\ncnnd.lib` (Debug)

### Building with CMake

#### Building with git bash
- Open git bash

```bash
# Clone the repository
git clone [<repository-url>](https://github.com/dicroce/revere.git)
cd revere

# Create build directory
mkdir build
cd build

# Configure
cmake ..

# Note: on windows Release is done while compiling not during previous configure step
cmake --build . --config Release

```
#### Installing
- Then open x64 Native Tools Command Prompt by right clicking and choosing "Run as Administrator"
- cd to the revere/build dir. Then:
```bash
cmake --build . --config Reelase --target install
```

## Verified Build Configurations

We regularly test these configurations:

| OS | Compiler | OpenCV | GStreamer | FFmpeg | Status |
|----|----------|--------|-----------|--------|--------|
| Ubuntu 22.04 | GCC 11 | 4.x | 1.20 | 4.x/5.x | ✅ |
| Windows 11 | MSVC 2022 (v17) | 4.12.0 | 1.0 | 6.x | ✅ |
| Fedora | GCC 11+ | 4.x | 1.0 | 4.x/5.x | 🟡 Should work |
| Arch Linux | GCC/Clang | 4.x | 1.0 | 4.x/5.x/6.x | 🟡 Should work |

## Getting Help

If you encounter issues not covered here, please:
1. Check the [Troubleshooting Guide](TROUBLESHOOTING.md)
2. Search existing [GitHub Issues]
3. Open a new issue with:
   - Your platform and versions
   - Full build log
   - Steps to reproduce
