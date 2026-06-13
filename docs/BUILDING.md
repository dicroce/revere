# Building Revere from Source

This guide covers **setting up a build environment** on each platform. Once your
environment is ready, the actual build is driven by the scripts in `scripts/` —
you shouldn't need to invoke CMake by hand.

## Table of Contents

- [Common prerequisites](#common-prerequisites)
- [Linux](#linux)
- [macOS](#macos)
- [Windows](#windows)
- [Optional: NCNN (AI detection)](#optional-ncnn-ai-detection)
- [Build options](#build-options)
- [Verified build configurations](#verified-build-configurations)
- [Getting help](#getting-help)

## Common prerequisites

- CMake 3.14 or higher
- A C++17 compatible compiler
- Git
- OpenCV 4.x, GStreamer 1.0, and FFmpeg 4.x/5.x/6.x (installed per-platform below)

---

## Linux

### Set up the build environment

#### Ubuntu / Debian

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

(Optional: for the AI detection plugins, also build NCNN and export `NCNN_TOP_DIR`
— see [Optional: NCNN](#optional-ncnn-ai-detection).)

#### Fedora / RHEL

Not yet documented — the equivalent `-devel` packages should work. Contributions welcome.

#### Arch Linux

Not yet documented — the equivalent packages should work. Contributions welcome.

### Build

Clone the repo, build as a normal user, then install as root:

```bash
git clone https://github.com/dicroce/revere.git
cd revere
./scripts/linux/build.sh                 # configure + build (run as a normal user)
sudo ./scripts/linux/build.sh --install  # install to /usr/local/revere
```

- The build step refuses to run as root (it would leave root-owned files); the
  install step must be run as root.
- Extra CMake args are passed through to the build step, e.g.
  `./scripts/linux/build.sh -DEXTERNAL_PLUGIN_REPOS=/path/to/revere_cloud`.

After installing, run `/usr/local/revere/revere`, or set Revere up as an always-on
headless service with `sudo /usr/local/revere/revere --install-service` (see
[Run Headless Service](../README.md#run-headless-service)).

To build the distributable **snap** instead, use `./scripts/linux/build_snap.sh`.

---

## macOS

### Set up the build environment

- macOS 10.15 (Catalina) or newer
- Xcode Command Line Tools: `xcode-select --install`
- [Homebrew](https://brew.sh/)

```bash
brew install cmake pkg-config opencv gstreamer gst-plugins-base \
             gst-plugins-good gst-plugins-bad ffmpeg glfw sqlite \
             mbedtls pugixml
```

### Build

```bash
git clone https://github.com/dicroce/revere.git
cd revere
python3 scripts/macos/build_revere_macos.py
```

This configures, builds, and produces a `.dmg` package.

---

## Windows

### Set up the build environment

#### Visual Studio

Download and install Visual Studio 2019 or newer from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/).
- Recommended: Visual Studio 2022 (MSVC v17)
- Required components: "Desktop development with C++"

#### Git

Installing Git also gives you Git Bash, which is handy for the clone and most steps.

#### CMake

Download from [cmake.org](https://cmake.org/download/) and install, or use Visual Studio's included CMake.

#### Native dependencies — prebuilt (recommended)

OpenCV, GStreamer, FFmpeg, and (optionally) NCNN are published as prebuilt
Windows x64 zips at [revere-deps](https://github.com/dicroce/revere-deps/releases/tag/windows-deps-v1)
— the same artifacts Revere's CI uses. This is the fast path; skip the manual
component installs below.

Download these assets from that release:
- `opencv-win64.zip`
- `gstreamer-win64.zip`
- `ffmpeg-win64.zip`
- `ncnn-win64.zip` *(optional — only needed for the AI detection plugins)*

Each zip is packaged whole-tree (its root contains `bin/`, `include/`, `x64/`,
etc. directly — there is no wrapping folder). Extract each into its own folder and
point the matching environment variable at that folder:

| Asset | Extract to (example) | Environment variable |
|-------|----------------------|----------------------|
| `opencv-win64.zip` | `C:\revere-deps\opencv` | `OPENCV_TOP_DIR` |
| `gstreamer-win64.zip` | `C:\revere-deps\gstreamer` | `GST_TOP_DIR` |
| `ffmpeg-win64.zip` | `C:\revere-deps\ffmpeg` | `FFMPEG_TOP_DIR` |
| `ncnn-win64.zip` | `C:\revere-deps\ncnn` | `NCNN_TOP_DIR` |

For example, in PowerShell (`setx` persists the variables for your user — open a
new terminal afterwards so they take effect):

```powershell
setx OPENCV_TOP_DIR  C:\revere-deps\opencv
setx GST_TOP_DIR     C:\revere-deps\gstreamer
setx FFMPEG_TOP_DIR  C:\revere-deps\ffmpeg
setx NCNN_TOP_DIR    C:\revere-deps\ncnn
```

That's all the dependency setup needed to build. The build copies the required
GStreamer/FFmpeg runtime DLLs next to the executable, so you don't need to touch
`PATH` for a normal build + install.

#### Native dependencies — build or install them yourself

Prefer to supply your own builds (or need a different version)? Install each
component manually and set the same `*_TOP_DIR` variables.

##### OpenCV

Download OpenCV 4.x prebuilt binaries from [opencv.org](https://opencv.org/releases/).
- Extract to a location like `C:\opencv`
- Set environment variable: `OPENCV_TOP_DIR=C:\opencv\build`
- Required structure: `OPENCV_TOP_DIR\x64\vc17\lib` (for VS 2022) or `vc16` (for VS 2019)
- The build system expects OpenCV 4.12.0 or compatible

##### GStreamer

Download GStreamer 1.0 MSVC runtime and development installers from [gstreamer.freedesktop.org](https://gstreamer.freedesktop.org/download/).
- Install both "runtime" and "development" packages
- Choose "Complete" installation to get all plugins
- Set environment variable: `GST_TOP_DIR=C:\gstreamer\1.0\msvc_x86_64`
- Add the GStreamer bin directory to PATH: `C:\gstreamer\1.0\msvc_x86_64\bin`

##### FFmpeg

Download FFmpeg shared libraries from [ffmpeg.org](https://ffmpeg.org/download.html) or [gyan.dev](https://www.gyan.dev/ffmpeg/builds/).
- Extract to a location like `C:\ffmpeg`
- Set environment variable: `FFMPEG_TOP_DIR=C:\ffmpeg`
- Required DLLs: avcodec, avformat, avutil, swscale (version suffix varies by FFmpeg 4.x/5.x/6.x)
- Ensure the DLLs are on PATH or copied next to the executable after build

### Build

From an **x64 Native Tools Command Prompt for VS 2022**:

```bat
scripts\windows\build.bat
```

`build.bat` wraps `build_installers.py`, which configures, builds Revere in
Release, and produces the Windows installer.

---

## Optional: NCNN (AI detection)

NCNN enables AI-based person/vehicle detection plugins (YOLOv8, MobileNet, PicoDet).
Build and install it, then export `NCNN_TOP_DIR` before running the build script.

```bash
# Linux/macOS
mkdir $HOME/NCNN_INSTALL
git clone https://github.com/Tencent/ncnn.git
cd ncnn
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DNCNN_BUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=$HOME/NCNN_INSTALL ..
make -j$(nproc)
make install
export NCNN_TOP_DIR=$HOME/NCNN_INSTALL
```

On Windows, the easiest option is `ncnn-win64.zip` from
[revere-deps](https://github.com/dicroce/revere-deps/releases/tag/windows-deps-v1)
(see [Windows setup](#windows)); or build/download NCNN yourself and set
`NCNN_TOP_DIR` (with `lib\ncnn.lib` for Release / `lib\ncnnd.lib` for Debug).

## Build options

Pass these to the build script (which forwards them to CMake) as `-D<name>=<value>`:

| Option | Default | Purpose |
|--------|---------|---------|
| `CMAKE_BUILD_TYPE` | `Release` | `Release` or `Debug`. |
| `EXTERNAL_PLUGIN_REPOS` | (none) | Semicolon-separated paths to plugin repos. |
| `REVERE_PORTABLE_BUILD` | `OFF` | Drop `-march=native` so the binary runs on other CPUs (for redistributables / AppImage). |
| `PACKAGED_BUILD` | `OFF` | Use FHS install paths (`bin`/`lib`) instead of the single `/usr/local/revere` directory; used by the snap/flatpak builds. |

## Verified build configurations

We regularly test these configurations:

| OS | Compiler | OpenCV | GStreamer | FFmpeg | Status |
|----|----------|--------|-----------|--------|--------|
| Ubuntu 22.04 | GCC 11 | 4.x | 1.20 | 4.x/5.x | ✅ |
| Windows 11 | MSVC 2022 (v17) | 4.12.0 | 1.0 | 6.x | ✅ |
| macOS 10.15+ | Apple Clang | 4.x | 1.0 | 4.x/5.x/6.x | ✅ |
| Fedora | GCC 11+ | 4.x | 1.0 | 4.x/5.x | 🟡 Should work |
| Arch Linux | GCC/Clang | 4.x | 1.0 | 4.x/5.x/6.x | 🟡 Should work |

## Getting help

If you encounter issues not covered here, please:
1. Search existing [GitHub Issues](https://github.com/dicroce/revere/issues)
2. Open a new issue with:
   - Your platform and versions
   - Full build log
   - Steps to reproduce
