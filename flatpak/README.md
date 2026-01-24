# Building Revere Flatpak

## Prerequisites

Install Flatpak and flatpak-builder:

```bash
sudo apt install flatpak flatpak-builder
```

Add the Flathub repository:

```bash
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```

Install the SDK and runtime:

```bash
flatpak install flathub org.freedesktop.Platform//24.08 org.freedesktop.Sdk//24.08
```

## Building

From the `flatpak/` directory:

```bash
cd /home/td/code/revere/flatpak
flatpak-builder --user --install --force-clean build-dir io.github.dicroce.Revere.yaml
```

This will:
1. Build NCNN (neural network library for AI detection)
2. Build OpenCV with contrib modules (motion detection)
3. Build GStreamer RTSP Server
4. Build Revere and Vision applications
5. Install the Flatpak to your user installation

## Running

After installation, run the applications:

```bash
# Main surveillance application
flatpak run io.github.dicroce.Revere

# Playback viewer
flatpak run --command=vision io.github.dicroce.Revere
```

Or launch from your desktop environment's application menu.

## Storage Location

The Flatpak stores all data (database, logs, video recordings) in:
```
~/Documents/revere/
```

## Creating a Distributable Bundle

To create a `.flatpak` bundle file for distribution:

```bash
# First build to a repository
flatpak-builder --repo=repo --force-clean build-dir io.github.dicroce.Revere.yaml

# Create the bundle
flatpak build-bundle repo revere.flatpak io.github.dicroce.Revere
```

The resulting `revere.flatpak` can be installed on any system with:
```bash
flatpak install revere.flatpak
```

## Validating for Flathub Submission

Before submitting to Flathub, validate the manifest and build:

```bash
# Install the linter
flatpak install flathub org.flatpak.Builder

# Validate the manifest
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest io.github.dicroce.Revere.yaml

# Validate the built repository
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
```

## Flathub Submission

To submit to Flathub:

1. Fork https://github.com/flathub/flathub
2. Create a new branch named `io.github.dicroce.Revere`
3. Add the manifest file as `io.github.dicroce.Revere.yaml`
4. Submit a pull request
5. The Flathub team will review and provide feedback

See https://github.com/flathub/flathub/wiki/App-Submission for detailed guidelines.

## Troubleshooting

### GStreamer plugin issues
If you see GStreamer errors about missing plugins, ensure the `GST_PLUGIN_PATH`
environment variable is set correctly. The Flatpak manifest sets this to
`/app/lib/gstreamer-1.0`.

### Camera discovery issues
ONVIF discovery uses multicast UDP. Ensure your network allows multicast traffic
between the Flatpak sandbox and your cameras.

### Storage permission issues
If Revere cannot write to `~/Documents/revere`, check that the Flatpak has the
correct filesystem permissions:
```bash
flatpak info --show-permissions io.github.dicroce.Revere
```
