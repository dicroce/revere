# Flatpak Build Guide

## Running on a Clean Ubuntu 24.04

Install flatpak:

```bash
sudo apt install flatpak
```

Add the Flathub repository:

```bash
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```

Install the freedesktop runtime:

```bash
flatpak install flathub org.freedesktop.Platform//24.08
```

Install and run Revere:

```bash
flatpak install --user revere.flatpak
flatpak run io.github.dicroce.Revere
```

---

## Building from Source

### Prerequisites

Install flatpak-builder and the freedesktop SDK:

```bash
sudo apt install flatpak-builder
flatpak install flathub org.freedesktop.Platform//24.08 org.freedesktop.Sdk//24.08
```

## Clean Rebuild

```bash
cd flatpak
flatpak-builder --force-clean --repo=repo build-dir io.github.dicroce.Revere.yaml
```

## Generate Bundle

```bash
flatpak build-bundle repo revere.flatpak io.github.dicroce.Revere
```

## Install and Run

Install the bundle:

```bash
flatpak install --user revere.flatpak
```

Run:

```bash
flatpak run io.github.dicroce.Revere
```

## Quick Build and Install (Development)

For faster iteration during development, build and install directly:

```bash
cd flatpak
flatpak-builder --force-clean --user --install build-dir io.github.dicroce.Revere.yaml
flatpak run io.github.dicroce.Revere
```

## System Plugins

Users can install optional system plugins (e.g., cloud connectivity) by placing `.so` files in:

```
~/Documents/revere/revere/system_plugins/
```
