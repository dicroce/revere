# Revere

## Overview

Revere is an open source video surveillance system with ONVIF camera support, motion detection, recording, playback and object detection.

## Key Features

- ONVIF camera discovery and management
- Motion detection with AI plugin support
- Continuous recording with non motion pruning.
- RTSP streaming
- Cross-platform (Linux/Windows)
- Full featured API
- YOLOv8 object detection of people and cars.

## Screenshots

### Revere
- Runs in the system tray
- Pre-allocated storage means it wont suprise you by using up more hard drive than you have allocated to it.
  
![Alt text](/assets/screenshots/revere.jpg "Revere")

### Vision
- Connects to revere
![Alt text](/assets/screenshots/vision.jpg "Vision")

## Quick Start

The quickest way to try it is the .AppImage version on linux or the portable windows binary version.

### Prerequisites

1) Onvif cameras - Axis cameras are the most compatible. If you want me to get your camera working for you send me one and I'll take a crack at it.
2) A Windows or Linux PC.

### Installation

On Linux, simply run the AppImage. You can set the appimage as a startup application on your linux desktop.
On Windows, unzip the package and run the revere.exe. In both cases feel free to set revere.exe as a startup application.

### Adding Your First Camera

Onvif discoverable cameras should appear in the Discovered list. Once it appears click Record and follow the instructions. Revere will attempt to stream a little from the camera to measure its bitrate and then allow you to pick a storage file size (with retention estimates) for that stream. During the camera provisioning process you will be asked if you want to enable motion detection for this camera. To get motion data in the timeline bar (in vision) you will need to say yes here. Additionally, yolov8 object detection is only run on frames containing motion.

## Documentation

- [Building from Source](docs/BUILDING.md) - Detailed build instructions for all platforms
- [Architecture](docs/ARCHITECTURE.md) - System design and component overview
- [User Guide](docs/USER_GUIDE.md) - Complete usage documentation
- [Troubleshooting](docs/TROUBLESHOOTING.md) - Common issues and solutions
- [Contributing](CONTRIBUTING.md) - How to contribute to the project

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| Ubuntu 22.04+ | ✅ Tested | Requires GCC 9+ or Clang 10+ |
| Windows 10/11 | ✅ Tested | Requires Visual Studio 2019 or newer (MSVC v17/2022 recommended) |
| Other Linux | 🟡 Should work | Any distribution with C++17 compiler and required dependencies |

## System Requirements

### Minimum
Unknown.

### Recommended
A few streams should be possible on most computers... especially if you're not running a build doing object detection.

### Storage
Storage requirements vary based on number of cameras, resolution, bitrate, and retention period. Revere uses the camera's bitrate to estimate retention when configuring recording. Generally:
- 1080p camera at 2-4 Mbps: ~1-2 GB per hour per camera
- 4K camera at 8-15 Mbps: ~4-7 GB per hour per camera
- Consider SSD for better performance with multiple concurrent recordings

## Camera Compatibility

Revere supports ONVIF-compliant cameras with the following features:
- Automatic camera discovery via ONVIF WS-Discovery
- RTSP video streaming (H.264/H.265)
- Axis cameras are known to be highly compatible
- Most modern IP cameras with ONVIF Profile S/T should work

If you have a camera that doesn't work with Revere, the project maintainer is willing to work with you to add support.

## Technology Stack & Acknowledgements

**Core Media Processing:**
- **OpenCV** (4.x) - Computer vision, motion detection, and background subtraction
- **GStreamer** (1.0) - Media pipeline framework for RTSP streaming and video processing
- **FFmpeg** (4.x/5.x/6.x) - Video decoding, encoding, and format handling

**AI/ML (Optional):**
- **NCNN** - Neural network inference for AI-based motion detection plugins (YOLOv8)

**User Interface:**
- **ImGui** - Immediate-mode GUI framework
- **GLFW** - Window and input management
- **OpenGL** - Graphics rendering

**Networking & Data:**
- **ONVIF** - IP camera discovery and control
- **SQLite** - Metadata and configuration storage
- **nanots** - Time-series database for recording
- **mbedTLS** - TLS/SSL and cryptography
- **pugixml** - XML parsing for ONVIF communication

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.
