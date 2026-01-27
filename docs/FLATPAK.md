# Flatpak Build Guide

## Installing from Flathub

Once published on Flathub, install directly:

```bash
flatpak install flathub io.github.dicroce.Revere
flatpak run io.github.dicroce.Revere
```

---

## Running on a Clean Ubuntu

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
flatpak install flathub org.freedesktop.Platform//25.08
```

Install and run Revere from a bundle:

```bash
flatpak install --user revere.flatpak
flatpak run io.github.dicroce.Revere
```

---

## Building from Source

### Prerequisites

Install flatpak and the flatpak-builder from Flathub:

```bash
sudo apt install flatpak
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.flatpak.Builder
flatpak install flathub org.freedesktop.Platform//25.08 org.freedesktop.Sdk//25.08
```

### Quick Build and Install (Development)

For faster iteration during development, build and install directly:

```bash
cd flatpak
flatpak run org.flatpak.Builder --force-clean --user --install build io.github.dicroce.Revere.yaml
flatpak run io.github.dicroce.Revere
```

### Clean Rebuild

```bash
cd flatpak
flatpak run org.flatpak.Builder --force-clean --repo=repo build io.github.dicroce.Revere.yaml
```

### Generate Bundle

```bash
flatpak build-bundle repo revere.flatpak io.github.dicroce.Revere
```

### Install Bundle

```bash
flatpak install --user revere.flatpak
flatpak run io.github.dicroce.Revere
```

---

## System Plugins

Users can install optional system plugins (e.g., cloud connectivity) by placing `.so` files in:

```
~/Documents/revere/revere/system_plugins/
```
