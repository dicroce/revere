# Revere

[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![Get it from the Snap Store](https://snapcraft.io/revere/badge.svg)](https://snapcraft.io/revere)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Raspberry%20Pi-lightgrey)

Revere is an open source video surveillance system with ONVIF camera support, motion detection, recording, playback and object detection.

![Revere desktop application recording multiple cameras](/assets/screenshots/revere.jpg "Revere")

## Contents

- [Key Features](#key-features)
- [Screenshots](#screenshots)
- [Quick Start](#quick-start)
  - [Installation](#installation)
  - [Run Headless Service](#run-headless-service)
  - [Adding Your First Camera](#adding-your-first-camera)
  - [Keeping the Computer Awake](#keeping-the-computer-awake)
- [Documentation](#documentation)
- [Building from Source](#building-from-source-quick-reference)
- [Platform Support](#platform-support)
- [System Requirements](#system-requirements)
- [Camera Compatibility](#camera-compatibility)
- [Technology Stack & Acknowledgements](#technology-stack--acknowledgements)
- [License](#license)

## Key Features

- ONVIF camera discovery and management
- Video codecs supported: H.264, H.265
- Audio codecs supported: AAC, MuLaw, ALaw
- Motion detection with AI plugin support
- Continuous recording with optional pruning of non-motion video
- RTSP restreaming of live and recorded video
- Cross-platform (Linux/Windows/Mac, plus arm64 Linux builds for Raspberry Pi)
- Full featured API
- YOLOv8 object detection (people, cars, bikes, animals and other common objects), run only on frames containing motion
- Multi-camera viewer (Vision) with motion timeline, playback, export and a floating single-camera overlay mode
- Storage management: move recording files to a new drive or reset storage in-place via the camera Properties dialog
- Headless mode & WebUI

## Screenshots

### Revere
- The recording server. Runs quietly in the system tray (or headless) and handles discovery, recording, motion detection and restreaming.

![Revere system tray icon](/assets/screenshots/tray.jpg "Tray icon")
- Pre-allocated storage means it won't surprise you by using up more hard drive than you have allocated to it.

![Revere main window](/assets/screenshots/revere.jpg "Revere")

### Vision
- Connects to Revere and streams live or recorded video.
- Single, 2x2 and 4x4 layouts with drag-and-drop camera assignment.
- Timeline bar shows motion events (people, cars, bikes) and lets you jump to any point in the recording. Export clips straight from the timeline.

![Vision multi-camera viewer](/assets/screenshots/vision.jpg "Vision")

- Overlay mode: pop any camera out into a small borderless window that floats on your desktop — with an auto-hiding control bar (pin on top, volume, close) and its own motion timeline. Launch it from Vision or directly with `vision --camera <id>`.

![Vision overlay mode](/assets/screenshots/vision_overlay.jpg "Vision overlay mode")

### Web UI

Revere serves a web UI on http://localhost:8088/ (protected by a system password) — manage cameras, watch live views and play back recordings from a browser. It's the primary interface when running headless.

![Web UI config tab](/assets/screenshots/webui_cameras.jpg "Web UI Config")

![Web UI live view](/assets/screenshots/webui_live.jpg "Live")

![Web UI single camera view with motion timeline](/assets/screenshots/webui_single.jpg "Single Camera View")

## Quick Start

On Windows the quickest way to try Revere is to download a release and run the installer (and optionally the revere_cloud installer if you want cloud access).

On Linux the simplest way is to install the revere snap:

[![Get it from the Snap Store](https://snapcraft.io/en/dark/install.svg)](https://snapcraft.io/revere)

On a Raspberry Pi (or any other arm64 Debian-based board), download the `.deb` from the releases page and install it with `apt` — see [Raspberry Pi / arm64 Linux](#raspberry-pi--arm64-linux) below.

### Prerequisites

1) One or more IP cameras - Revere supports a wide variety of ONVIF-compatible cameras (Axis cameras are the most compatible, with Reolink a close second), and non-ONVIF cameras can be added by RTSP URL.
2) A Windows, Linux or macOS computer, or a Raspberry Pi 4/5 running 64-bit Raspberry Pi OS (bookworm or later).

### Installation

**Windows**
- Download and run the installer from the [GitHub releases page](https://github.com/dicroce/revere/releases), then choose Revere from the Start menu.

**macOS**
- Download and run the `.dmg` from the [GitHub releases page](https://github.com/dicroce/revere/releases), then drag Revere into your Applications folder.

**Linux**
- **AppImage** — download the `.AppImage` from the [GitHub releases page](https://github.com/dicroce/revere/releases), make it executable and run it:
  ```bash
  chmod +x Revere-*.AppImage
  ./Revere-*.AppImage
  ```
- **Snap** — install from the [Snap Store](https://snapcraft.io/revere), then launch it from the system menu or the command line:
  ```bash
  sudo snap install revere
  snap run revere
  ```
- **Build from source** — build and install to `/usr/local/revere` (see [Building from Source](docs/BUILDING.md) for the build prerequisites):
  ```bash
  mkdir -p build && cd build
  cmake .. -DCMAKE_BUILD_TYPE=Release
  cmake --build .
  sudo cmake --install .            # installs to /usr/local/revere
  ```
  Run it with `/usr/local/revere/revere`. To run it headless as a service instead, see [Run Headless Service](#run-headless-service) below.

**Raspberry Pi / arm64 Linux**

Every release also ships a native `aarch64` build for Raspberry Pi 4/5 and other arm64 Debian-based boards. It's packaged as a `.deb` built against Debian bookworm, so its dependencies resolve on 64-bit Raspberry Pi OS bookworm (or later) and on Debian/Ubuntu arm64 releases of the same age or newer. The 32-bit Pi OS is not supported.

- Download `revere-vX.Y.Z-linux-arm64.deb` from the [GitHub releases page](https://github.com/dicroce/revere/releases) and install it with `apt`, which pulls in the runtime dependencies:
  ```bash
  sudo apt install ./revere-v*-linux-arm64.deb
  ```
  The package installs to `/opt/revere` and adds Revere and Vision to the desktop menu, but it does not start anything on its own.
- Most people will want to run a Pi headless and manage it from the web UI. Install the `systemd` service (see [Run Headless Service](#run-headless-service) for details — note the binary lives at `/opt/revere/revere` rather than `/usr/local/revere/revere` on a `.deb` install):
  ```bash
  sudo /opt/revere/revere --install-service
  ```
  Then open `http://<pi-address>:8088/` from another machine on your LAN.
- Upgrading is just installing the newer `.deb` the same way; a running service is restarted automatically. `sudo apt remove revere` uninstalls the service but leaves your recordings in `/var/lib/revere`.
- For cloud access, releases include a matching arm64 plugin installer, `revere-vX.Y.Z-linux-arm64-cloud.run`. Point it at the service's data directory when installing into a headless setup:
  ```bash
  chmod +x revere-v*-linux-arm64-cloud.run
  sudo ./revere-v*-linux-arm64-cloud.run /var/lib/revere
  ```

### Run Headless Service

On Linux, Revere can run as a headless `systemd` service — recording, ONVIF discovery and the web UI (on http://localhost:8088/) with no desktop session. This is the recommended way to run Revere on a server, NAS or always-on mini-PC.

First build from source and install to `/usr/local/revere` using the same steps as the Linux [Installation](#installation) section above (`cmake …` then `sudo cmake --install .`). See [Building from Source](docs/BUILDING.md) for the full list of build prerequisites. On a Raspberry Pi or other arm64 board you can skip the build and install the release `.deb` instead (see [Raspberry Pi / arm64 Linux](#raspberry-pi--arm64-linux)); in that case substitute `/opt/revere` for `/usr/local/revere` in the commands below.

Then install and start the service. Run it as root — it creates a dedicated `revere` system user and the `/var/lib/revere` data directory, writes `/etc/systemd/system/revere.service`, and enables it to start on boot:

```bash
sudo /usr/local/revere/revere --install-service
```

Check on it with:

```bash
systemctl status revere
journalctl -u revere -f
```

Then open http://localhost:8088/ in a browser. On first run you'll be asked to set a system password, after which you can add and manage cameras from the web UI.

By default recordings, the database and the encryption key live under `/var/lib/revere`. To put them somewhere else (e.g. a dedicated storage volume), pass the path through at install time:

```bash
sudo /usr/local/revere/revere --install-service --data-dir /mnt/storage/revere
```

To remove the service (this leaves your recordings and the `revere` user intact):

```bash
sudo /usr/local/revere/revere --uninstall-service
```

You can also run headless manually, without installing a service, by pointing `--data-dir` at a directory you can write to:

```bash
/usr/local/revere/revere --headless --data-dir ~/revere-data
```

Run `revere --help` to see all command line options.

### Adding Your First Camera

ONVIF-discoverable cameras on your LAN appear in the Discovered list. Click Record next to a camera and follow the instructions: Revere streams briefly from the camera to measure its bitrate, then lets you pick a storage file size (with retention estimates) for that stream. During setup you'll be asked whether to enable motion detection for the camera — say yes if you want motion events in the timeline bar (in Vision and the web UI). YOLOv8 object detection only runs on frames containing motion, so it requires motion detection too.

Cameras that don't support ONVIF discovery can still be added manually by entering their RTSP URL (Cameras menu in the desktop app, or "Add RTSP Camera" in the web UI Config tab).

### Keeping the Computer Awake

A computer that goes to sleep stops recording. While it's plugged in, Revere keeps the computer from going to sleep on its own; a laptop on battery sleeps as usual. The display still turns off. Change this with **Keep this computer awake** and **Also on battery** in the desktop app's Settings menu or the web UI's Config tab (or in `config/keep_awake.json` in the data directory: `{"enabled": true, "on_battery": false}`).

Revere holds the request under the name "Revere is recording cameras". You can see it with `pmset -g assertions` on macOS, `powercfg /requests` (as administrator) on Windows, and on Linux your desktop's own tools or, for the headless service, `systemd-inhibit --list`. On a Linux desktop (the Snap included) Revere asks the desktop itself (GNOME, or KDE and Xfce through freedesktop PowerManagement), which stops automatic suspend but still lets you choose Suspend; the Snap does this through the `screen-inhibit-control` interface. The headless service has no desktop, so it takes a logind sleep inhibitor instead, allowed by a polkit rule that `--install-service` writes; while it's held, a suspend asked for through logind needs an administrator. A desktop with no suspend inhibitor to ask (sway or i3, for example) shows Keep this computer awake as not available; its own power settings decide when it sleeps.

If the computer sleeps anyway, Revere logs it when it wakes, and the web UI lists recent sleeps under Keep this computer awake.

**Recording with the lid closed.** Closing a laptop's lid puts it to sleep whatever this setting says. To keep recording with it closed:
- **macOS:** connect power, an external display and a keyboard or mouse. A Mac then stays awake with its lid closed.
- **Windows:** Control Panel → Power Options → *Choose what closing the lid does* → *When plugged in*: **Do nothing**.
- **Linux:** set `HandleLidSwitchExternalPower=ignore` in `/etc/systemd/logind.conf` and restart. Some desktops (GNOME, KDE) have their own lid setting too.

## Documentation

- [Building from Source](docs/BUILDING.md) - Detailed build instructions for all platforms
- [Architecture](docs/ARCHITECTURE.md) - System design and component overview

## Building from Source (quick reference)

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

To include the optional `revere_cloud` plugin:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DEXTERNAL_PLUGIN_REPOS=/path/to/revere_cloud
```

Multiple plugin repos can be separated by semicolons.

## Platform Support

| Platform | Status |
|----------|--------|
| Ubuntu 22.04+ (x86_64) | ✅ Tested |
| Windows 10/11 | ✅ Tested |
| macOS 10.15+ | ✅ Tested |
| Snap | ✅ Tested |
| Raspberry Pi OS 64-bit, bookworm+ (arm64 `.deb`) | ✅ Tested on Raspberry Pi 5 |
| Other arm64 Debian/Ubuntu (bookworm-era or newer) | ⚠️ Should work (same `.deb`), untested |

## System Requirements

### Minimum
For the desktop apps (Revere + Vision) with several cameras, plan on a modern desktop or laptop CPU — smooth multi-camera decoding and playback is the expensive part.

### Raspberry Pi
The arm64 `.deb` is aimed at running Revere headless on a Raspberry Pi 4 or 5 as a small always-on recorder, managed through the web UI. Recording and RTSP restreaming are light, but motion detection and especially YOLOv8 object detection are CPU-bound, so keep the camera count and the number of cameras with analytics enabled modest, and expect a Pi 4 to handle less than a Pi 5. The desktop apps install too, but the Pi is not a good fit for viewing many cameras at once — use Vision or the web UI from another machine instead. Record to a USB SSD rather than the SD card.

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
- SDL - Graphics rendering

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
