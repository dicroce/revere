# Revere

## Overview

Revere is an open source video surveillance system with ONVIF camera support, motion detection, recording, playback and object detection.

**Revere is extremely beta software at the moment. I have only been able to test this on a small number of cameras.**

## Key Features

- ONVIF camera discovery and management
- Video codecs supported: H.264, H.265
- Audio codecs supported: AAC, MuLaw, ALaw
- Motion detection with AI plugin support
- Continuous recording with non motion pruning
- RTSP streaming
- Cross-platform (Linux/Windows/Mac)
- Full featured API
- YOLOv8 object detection of people and cars

## Screenshots

### Revere
- Runs in the system tray

![Alt text](/assets/screenshots/tray.jpg "Tray icon")
- Pre-allocated storage means it wont suprise you by using up more hard drive than you have allocated to it.
  
![Alt text](/assets/screenshots/revere.jpg "Revere")

### Vision
- Connects to revere
- Supports 2x2, 4x4 and single view.

![Alt text](/assets/screenshots/vision.jpg "Vision")

## Quick Start

On Windows the quickest way to try Revere is to download a release and run the installer (and optionally the revere_cloud installer if you want cloud access).

On Linux the simplest way is to install the revere snap:

[![Get it from the Snap Store](https://snapcraft.io/en/dark/install.svg)](https://snapcraft.io/revere)

### Prerequisites

1) Onvif cameras - Revere should support a wide variety of Onvif compatible cameras.
2) Axis cameras are the most compatible with Reolink a close second. If you want me to get your camera working for you send me one and I'll take a crack at it.
3) Either a Windows, Linux or MacOS computer.

### Installation

On Linux, install the snap and type 'snap run revere' (or double click on Revere in the system menu).
On Windows run the installer and then choose Revere in the start menu.

### Adding Your First Camera

Onvif discoverable cameras on your LAN 
should appear in the Discovered list. Once it appears click Record and follow the instructions. Revere will attempt to stream a little from the camera to measure its bitrate and then allow you to pick a storage file size (with retention estimates) for that stream. During the camera provisioning process you will be asked if you want to enable motion detection for this camera. To get motion data in the timeline bar (in vision) you will need to say yes here. Additionally, yolov8 object detection is only run on frames containing motion.

## Documentation

- [Building from Source](docs/BUILDING.md) - Detailed build instructions for all platforms
- [Architecture](docs/ARCHITECTURE.md) - System design and component overview
- [User Guide](docs/USER_GUIDE.md) - Complete usage documentation
- [Troubleshooting](docs/TROUBLESHOOTING.md) - Common issues and solutions
- [Contributing](CONTRIBUTING.md) - How to contribute to the project

## Platform Support

| Platform | Status |
|----------|--------|
| Ubuntu 22.04+ | ✅ Tested |
| Windows 10/11 | ✅ Tested |
| macOS 10.15+ | ✅ Tested |
| Snap | ✅ Tested |
| flatpak | ✅ Tested |

## System Requirements

### Minimum
A resonably modern computer is required. I built it on the pi5 and found it was unusably slow.

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

If you have a camera that doesn't work with Revere send me a message, maybe we can get it going.

## Technology Stack & Acknowledgements

**Core Media Processing:**
- [OpenCV](https://opencv.org/) (4.x) - Computer vision, motion detection, and background subtraction
- [GStreamer](https://gstreamer.freedesktop.org/) (1.0) - Media pipeline framework for RTSP streaming and video processing
- [FFmpeg](https://ffmpeg.org/) - Video decoding, encoding, and format handling

**AI/ML (Optional):**
- [NCNN](https://github.com/Tencent/ncnn) - High-performance neural network inference (YOLOv8 object detection)

**User Interface:**
- [Dear ImGui](https://github.com/ocornut/imgui) (v1.88) - Immediate-mode GUI framework
- [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog) - File dialog extension for ImGui
- [GLFW](https://www.glfw.org/) (3.4) - Cross-platform window and input management
- OpenGL - Graphics rendering

**Storage & Data:**
- [nanots](https://github.com/dicroce/nanots) - Time-series storage for video recording
- [SQLite](https://sqlite.org/) - Metadata and configuration storage (bundled with nanots)
- [nlohmann/json](https://github.com/nlohmann/json) - JSON parsing

**Networking & Security:**
- [pugixml](https://pugixml.org/) - XML parsing for ONVIF communication
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (3.6.3) - TLS/SSL and cryptography

**Utilities:**
- [date](https://github.com/HowardHinnant/date) - Howard Hinnant's date/time library
- [stb libraries](https://github.com/nothings/stb) - Single-file image processing utilities

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.
