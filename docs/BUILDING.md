# Building Revere from Source

This guide covers building Revere on all supported platforms.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Building on Linux](#building-on-linux)
  - [Ubuntu/Debian](#ubuntudebian)
  - [Fedora/RHEL](#fedorarhel)
  - [Arch Linux](#arch-linux)
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
cmake .. -DCMAKE_BUILD_TYPE=Release

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

## Building on Windows

### Installing Dependencies

#### Visual Studio

Download and install Visual Studio 2019 or newer from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/).
- Recommended: Visual Studio 2022 (MSVC v17)
- Required components: "Desktop development with C++"

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

### Setting Environment Variables

You must set the following environment variables for CMake to find the dependencies:

```powershell
# Set temporarily (for current session)
$env:OPENCV_TOP_DIR = "C:\path\to\opencv"
$env:GST_TOP_DIR = "C:\path\to\gstreamer"
$env:FFMPEG_TOP_DIR = "C:\path\to\ffmpeg"
$env:NCNN_TOP_DIR = "C:\path\to\ncnn"  # Optional

# Or set permanently (all sessions)
[System.Environment]::SetEnvironmentVariable("OPENCV_TOP_DIR", "C:\path\to\opencv", "User")
[System.Environment]::SetEnvironmentVariable("GST_TOP_DIR", "C:\path\to\gstreamer", "User")
[System.Environment]::SetEnvironmentVariable("FFMPEG_TOP_DIR", "C:\path\to\ffmpeg", "User")
```

#### Required Directory Structure

**OPENCV_TOP_DIR:**
```
OPENCV_TOP_DIR/
├── include/
│   └── opencv2/
├── x64/
│   └── vc17/  (or vc16 for VS2019)
│       ├── lib/
│       └── bin/
```

**GST_TOP_DIR:**
```
GST_TOP_DIR/
├── include/
│   └── gstreamer-1.0/
├── lib/
└── bin/
```

**FFMPEG_TOP_DIR:**
```
FFMPEG_TOP_DIR/
├── include/
│   ├── libavcodec/
│   ├── libavformat/
│   ├── libavutil/
│   └── libswscale/
├── lib/
└── bin/  (contains DLLs)
```

### Building with CMake

#### Using CMake GUI

1. Open CMake GUI
2. Set "Where is the source code" to your Revere directory
3. Set "Where to build the binaries" to `<revere>/build`
4. Click "Configure" and select your Visual Studio version
5. Verify all dependencies are found (check the output)
6. Click "Generate"
7. Click "Open Project" to launch Visual Studio

#### Using Command Line

```powershell
# Open Developer Command Prompt for VS
# Navigate to revere directory
cd C:\path\to\revere

# Create build directory
mkdir build
cd build

# Configure
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
cmake --build . --config Release

# Or open the solution in Visual Studio
start revere.sln
```

#### Building from Visual Studio

1. Open the generated `revere.sln` solution file
2. Select your desired build configuration (Debug or Release)
3. Build > Build Solution (or press F7)
4. The executables will be in `build\apps\revere\<Config>\` and `build\apps\vision\<Config>\`
5. Copy required DLLs to the executable directory or ensure they're in your PATH

## Build Options

### CMake Options

**CMAKE_BUILD_TYPE:** Debug, Release, or RelWithDebInfo (Linux/Mac)

```bash
# Release build (optimized)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Debug build (with AddressSanitizer on Linux)
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

**NCNN_TOP_DIR:** Path to NCNN installation (enables AI plugins)

```bash
cmake .. -DNCNN_TOP_DIR=/path/to/ncnn
```

### Build Configurations

**Debug:**
- Includes debug symbols
- No optimization
- Linux: Enables AddressSanitizer for memory error detection
- Windows: Runtime checks enabled (/RTC1)

**Release:**
- Full optimization (-O3 on Linux, /O2 on Windows)
- No debug symbols
- Best performance
- Recommended for production use

**RelWithDebInfo:**
- Optimized but with debug symbols
- Useful for profiling and debugging performance issues

## Optional Dependencies

### NCNN (Neural Network Framework)

**Purpose:** Enables AI-based motion detection plugins for person detection

**Plugins enabled:**
- YOLOv8 Person Plugin (currently active)
- MobileNet Person Plugin (available but disabled by default)
- PicoDet Person Plugin (available but disabled by default)

**Building with NCNN:**
```bash
# Set NCNN_TOP_DIR environment variable
export NCNN_TOP_DIR=/path/to/ncnn  # Linux
set NCNN_TOP_DIR=C:\path\to\ncnn   # Windows

# Configure with NCNN support
cmake .. -DNCNN_TOP_DIR=/path/to/ncnn
```

**Building without NCNN:**
Simply don't set `NCNN_TOP_DIR`. The system will build without AI plugin support. The basic motion detection will still work.

## Running Tests

```bash
# From build directory
ctest

# Or run with verbose output
ctest -V

# Or run individual test binaries
./libs/r_av/ut/test_r_av
./libs/r_db/ut/test_r_db
./libs/r_disco/ut/test_r_disco
./libs/r_http/ut/test_r_http
./libs/r_motion/ut/test_r_motion
./libs/r_onvif/ut/test_r_onvif
./libs/r_pipeline/ut/test_r_pipeline
./libs/r_storage/ut/test_r_storage
./libs/r_utils/ut/test_r_utils
```

### Test Requirements

Most tests are unit tests that don't require external dependencies. Some integration tests may require:
- Network access for ONVIF discovery tests
- Test video files (generated automatically where needed)
- Write permissions in the test directory

## Common Build Issues

### OpenCV Not Found

**Symptoms:**
[Error messages]

**Solutions:**
[How to fix]

### GStreamer Linking Errors

**Symptoms:**
[Error messages]

**Solutions:**
[How to fix]

### Windows: Missing DLL at Runtime

**Symptoms:**
```
The code execution cannot proceed because opencv_world4120.dll was not found
The code execution cannot proceed because avcodec-62.dll was not found
The code execution cannot proceed because gstreamer-1.0-0.dll was not found
```

**Solutions:**

1. **Add to PATH:**
   ```powershell
   # Add directories to PATH
   $env:PATH += ";C:\opencv\build\x64\vc17\bin"
   $env:PATH += ";C:\gstreamer\1.0\msvc_x86_64\bin"
   $env:PATH += ";C:\ffmpeg\bin"
   ```

2. **Copy DLLs:** Copy all required DLLs to the same directory as the executable

3. **Verify DLL locations:**
   ```powershell
   where opencv_world4120.dll
   where avcodec-62.dll
   ```

### Windows: MSVC Version Mismatch

**Symptoms:**
```
opencv_world4120.lib was built with vc17 but you're using vc16
```

**Solution:**
Ensure your OpenCV version matches your Visual Studio version (vc17 for VS2022, vc16 for VS2019), or rebuild OpenCV from source with your compiler.

### Linux: Permission Denied When Running

**Symptoms:**
```
./revere: Permission denied
```

**Solution:**
```bash
chmod +x build/apps/revere/revere
chmod +x build/apps/vision/vision
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
