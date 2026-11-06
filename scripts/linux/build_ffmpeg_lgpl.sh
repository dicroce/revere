#!/bin/bash
#
# Builds Revere's own FFmpeg, LGPL only, into a prefix to use as FFMPEG_TOP_DIR
# (the CMake install then ships its libraries next to revere).
#
# The closed Revere Cloud and HomeKit plugins load into revere's process, and
# the distros' FFmpeg packages are GPL builds, so revere doesn't use them.
# Without --enable-gpl or --enable-nonfree, FFmpeg's configure refuses every GPL
# or nonfree component, whatever it autodetects; this checks it anyway.
#
# Software H.264 encoding, x264's job in a GPL build, is OpenH264 (BSD), linked
# into libavcodec statically so there's no extra library to ship.
#
# Usage: build_ffmpeg_lgpl.sh <prefix> [extra FFmpeg configure flags...]
# Env:   FFMPEG_VERSION   FFmpeg git tag   (default n8.1.3, as Windows/macOS)
#        OPENH264_VERSION OpenH264 git tag (default v2.6.0)
# Needs: build tools, git, pkg-config, libwebp-dev, libopus-dev; nasm on x86_64.

set -euo pipefail

PREFIX="$1"
shift
FFMPEG_VERSION="${FFMPEG_VERSION:-n8.1.3}"
OPENH264_VERSION="${OPENH264_VERSION:-v2.6.0}"
JOBS="$(nproc)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> OpenH264 $OPENH264_VERSION (static, PIC)"
git clone -q --depth 1 --branch "$OPENH264_VERSION" https://github.com/cisco/openh264.git "$WORK/openh264"
make -C "$WORK/openh264" -j"$JOBS" CFLAGS_OPT="-O3 -fPIC" PREFIX="$PREFIX" install-static

echo "==> FFmpeg $FFMPEG_VERSION (LGPL, shared)"
git clone -q --depth 1 --branch "$FFMPEG_VERSION" https://github.com/FFmpeg/FFmpeg.git "$WORK/ffmpeg"
cd "$WORK/ffmpeg"
# --static: OpenH264's .pc lists the C++ runtime it needs as a private lib.
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}" ./configure \
    --prefix="$PREFIX" \
    --enable-shared --disable-static \
    --pkg-config-flags=--static \
    --disable-programs --disable-doc --disable-debug \
    --disable-avdevice --disable-avfilter \
    --disable-sdl2 --disable-xlib --disable-libxcb \
    --enable-libopenh264 --enable-libwebp --enable-libopus \
    "$@"
if ! grep -q "^#define CONFIG_GPL 0" config.h || ! grep -q "^#define CONFIG_NONFREE 0" config.h; then
    echo "Error: FFmpeg was configured with GPL or nonfree parts"
    exit 1
fi
make -j"$JOBS"
make install

# OpenH264 is inside libavcodec now; only FFmpeg's shared libraries ship.
rm -f "$PREFIX"/lib/libopenh264.a
echo "==> Installed:"
ls -la "$PREFIX/lib"
