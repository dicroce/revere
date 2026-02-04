# Revere Snap Package

This directory contains the Snap packaging configuration for Revere.

## Prerequisites

Install snapcraft:
```bash
sudo snap install snapcraft --classic
```

## Building

Build the snap package:
```bash
cd /path/to/revere
snapcraft
```

This will create `revere_<version>_amd64.snap` in the current directory.

## Installing

Install the locally built snap:
```bash
sudo snap install revere_*.snap --dangerous
```

The `--dangerous` flag is required for locally built snaps that aren't signed.

## Running

Run the main surveillance application:
```bash
revere
```

Run the playback/viewer application:
```bash
revere.vision
```

## Permissions

The snap requests the following permissions:
- `home` - Access to home directory for storing recordings in ~/Documents/revere
- `network` - Network access for RTSP streams and ONVIF discovery
- `network-bind` - Ability to bind network ports for the REST API
- `opengl` - GPU acceleration for video rendering
- `audio-playback` - Audio output
- `camera` - Camera access (for local webcams if needed)

If you need to grant additional permissions:
```bash
sudo snap connect revere:camera
```

## Clean Build

To start fresh:
```bash
snapcraft clean
snapcraft
```

## Notes

- The snap uses strict confinement for security
- Recordings are stored in ~/Documents/revere (accessible via the home plug)
- GStreamer plugins are bundled in the snap
- The gnome extension provides GTK/desktop integration
