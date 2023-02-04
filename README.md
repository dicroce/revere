# Revere
## Open Source Video Surveillance
### Features
- Onvif camera compatibility & discovery
- H.264, H.265, AAC, G.711, G.726
- Up to 4K resolutions
- Windows & Linux support
- Unobtrusive background application that will only use the disk space you allocate to it.

![](assets/revere.png)

![](assets/vision.png)

### What Works
- Onvif camera discovery
- Recording
- RTSP restreaming
- Live viewing
- Scrub Bar
- Motion Detection
- Extensive REST API: (/cameras,/contents,/jpg,/motions,/export), etc.
- Exports

## Compiling on Ubuntu Desktop 22.04 LTS

1) Update your system.
2) If this is a virtualbox VM
  - Have the guest additions installed
  - Have at least 64gb of vram.
  - Choose bridged networking for Onvif camera discovery
3) Base packages
```
sudo apt install git curl zip unzip tar cmake pkg-config nasm libxinerama-dev libxcursor-dev xorg-dev libglu1-mesa-dev bison python3-distutils flex libgtk-3-dev libayatana-appindicator3-dev libunwind-dev build-essential
```
4) Building
```
git clone https://github.com/dicroce/revere --recursive
mkdir revere/build && pushd revere/build && cmake .. && make && sudo make install
```
At this point you should have revere installed in /usr/local/revere and vision installed in /usr/local/vision

## Compiling on Windows
1) Download and install git for windows from https://git-scm.com
2) Download and install Visual Studio 2019 (free Community edition is fine since Revere is opensource).
3) Launch the Git Bash prompt that comes with git for windows.
4) Type the following commands
```
git clone https://github.com/dicroce/revere --recursive
mkdir revere/build && pushd revere/build && cmake .. && cmake --build . --target install
```
NOTE: the "cmake --build . --target install" target works on Windows but it has to be run from a Git Bash shell run with Administrator privelages. It will install Revere and Vision to C:\Program Files (x86)\revere.
