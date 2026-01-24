# Building Revere on macOS

This guide covers building Revere from source on macOS systems.

## Prerequisites

### System Requirements
- macOS 10.15 (Catalina) or newer
- Xcode Command Line Tools
- Homebrew package manager
- At least 8GB RAM
- 10GB free disk space

### Installing Xcode Command Line Tools

```bash
xcode-select --install
```

### Installing Homebrew

If you don't have Homebrew installed:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## Installing Dependencies

### Required Dependencies

Run the following commands to install all required dependencies:

```bash
# Update Homebrew
brew update

# Install build tools
brew install cmake pkg-config

# Install OpenCV
brew install opencv

# Install GStreamer
brew install gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav gst-rtsp-server

# Install FFmpeg
brew install ffmpeg

# Install other dependencies
brew install glfw3
brew install sqlite3
brew install mbedtls
brew install pugixml
```

### Optional: NCNN for AI Motion Detection

If you want to build with AI-based motion detection support:

```bash
# Clone and build NCNN
mkdir -p ~/NCNN_INSTALL
git clone https://github.com/Tencent/ncnn.git
cd ncnn
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DNCNN_BUILD_EXAMPLES=OFF \
      -DCMAKE_INSTALL_PREFIX=$HOME/NCNN_INSTALL \
      ..
make -j$(sysctl -n hw.ncpu)
make install

# Set environment variable (add to ~/.zshrc or ~/.bash_profile)
export NCNN_TOP_DIR=$HOME/NCNN_INSTALL
```

## Building Revere

### Clone the Repository

```bash
git clone https://github.com/dicroce/revere.git
cd revere
```

### Configure and Build

```bash
# Create build directory
mkdir build
cd build

# Configure with CMake
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build (using all available CPU cores)
make -j$(sysctl -n hw.ncpu)
```

### Installation

```bash
# Install to /usr/local/revere
sudo make install
```

The executables will be in:
- `build/apps/revere/revere` - Main surveillance system
- `build/apps/vision/vision` - Viewer application

## Running Revere

### First Run

1. Start the Revere service:
   ```bash
   ./build/apps/revere/revere
   ```

2. In a separate terminal, start the Vision viewer:
   ```bash
   ./build/apps/vision/vision
   ```

### Setting up as a Launch Agent (Optional)

To run Revere automatically at login:

1. Create a launch agent plist file:

```bash
cat > ~/Library/LaunchAgents/com.revere.service.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.revere.service</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/revere/bin/revere</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
EOF
```

2. Load the launch agent:
```bash
launchctl load ~/Library/LaunchAgents/com.revere.service.plist
```

## Troubleshooting

### Common Issues

#### 1. CMake Can't Find Dependencies

If CMake can't find installed dependencies, ensure Homebrew's pkg-config path is set:

```bash
export PKG_CONFIG_PATH="/usr/local/opt/opencv@4/lib/pkgconfig:$PKG_CONFIG_PATH"
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
```

Add these to your shell profile (`~/.zshrc` or `~/.bash_profile`).

#### 2. GStreamer Plugin Issues

If you encounter GStreamer plugin errors:

```bash
# Set GStreamer plugin path
export GST_PLUGIN_PATH="/usr/local/lib/gstreamer-1.0"

# Check available plugins
gst-inspect-1.0
```

#### 3. OpenGL/GLFW Issues

If you get OpenGL-related errors:

```bash
# Reinstall GLFW with correct flags
brew reinstall glfw --HEAD
```

#### 4. Build Errors with C++17

Ensure you're using a modern enough compiler:

```bash
# Check compiler version
clang++ --version

# Should be Apple clang version 11.0 or newer
```

### Debug Build

For development and debugging:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(sysctl -n hw.ncpu)
```

### Clean Build

If you need to start fresh:

```bash
cd build
make clean
# Or completely remove and recreate build directory
cd ..
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
```

## Platform-Specific Notes

### macOS Security and Privacy

When running Revere for the first time, macOS may prompt for:
- Camera access permissions
- Network access permissions
- File system access permissions

Grant these permissions through System Preferences > Security & Privacy.

### Performance Considerations

- For best performance, disable macOS App Nap for Revere
- Consider using an SSD for recording storage
- Close unnecessary applications to free up memory for multiple camera streams

## Getting Help

If you encounter issues not covered here:
1. Check the main [Troubleshooting Guide](TROUBLESHOOTING.md)
2. Search existing [GitHub Issues](https://github.com/dicroce/revere/issues)
3. Open a new issue with:
   - Your macOS version
   - Full build log
   - Output of `brew list --versions`
   - CMake configuration output