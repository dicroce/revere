#!/usr/bin/env bash
# Runs the harness in a Linux container, for machines that are not Linux (or to keep it apart).
# Started by: tools/harness/run.sh mac <command>
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
harness="$(dirname "$here")"
repo="$(cd "$harness/../.." && pwd)"
image="${REVERE_HARNESS_IMAGE:-revere-harness:local}"
name="revere-harness"
port="${HARNESS_PORT:-8090}"

usage() {
    cat <<'TEXT'
usage: tools/harness/run.sh mac <command>
  build [--no-cache]            build the image (done automatically the first time)
  <suite>... [-- <arguments>]   run suites, for example:  mac smoke
                                                          mac recording stress -- --param minutes=1
  web                           the control page on http://127.0.0.1:8090 (HARNESS_PORT changes the port)
  test                          the harness's unit tests
  shell                         a shell in the container
  size                          how much disk the image and Docker use
  prune                         drop the build cache but keep the image (frees most of the build's disk)
  clean                         remove the container, the image and the build cache
TEXT
}

if command -v docker >/dev/null 2>&1; then
    docker="docker"
elif [ -x "$HOME/.rd/bin/docker" ]; then
    docker="$HOME/.rd/bin/docker"       # Rancher Desktop, when its folder is not on the PATH
else
    echo "error: no docker command found; install Docker Desktop or Rancher Desktop (dockerd engine)" >&2
    exit 1
fi

case "${1:-help}" in help|-h|--help) usage; exit 0 ;; esac

if ! "$docker" info >/dev/null 2>&1; then
    echo "error: the Docker engine is not running" >&2
    exit 1
fi

# A real recorder may be running on this machine: its ports are never published.
case "$port" in
    8088|8443|8554|3702) echo "error: HARNESS_PORT $port is one of the recorder's own ports" >&2; exit 1 ;;
    ''|*[!0-9]*) echo "error: HARNESS_PORT must be a number" >&2; exit 1 ;;
esac

image_mb() { echo "$(( $("$docker" image inspect --format '{{.Size}}' "$image") / 1024 / 1024 )) MB"; }

build_image() {
    "$docker" build -f "$here/Dockerfile" -t "$image" "$@" "$repo"
    echo "image $image: $(image_mb)"
}

ensure_image() {
    "$docker" image inspect "$image" >/dev/null 2>&1 || { echo "building $image (first time only)"; build_image; }
}

run_container() {
    mode="$1"; shift
    if [ -n "$("$docker" ps -q --filter "name=^${name}$")" ]; then
        echo "error: a container named $name is already running" >&2
        exit 1
    fi
    mkdir -p "$harness/out"
    args=(run --rm --name "$name" --init
          # What the harness's own sandbox needs: new namespaces, mounts inside them and
          # virtual network links. Not --privileged: the container gets no host devices.
          --cap-add SYS_ADMIN --cap-add NET_ADMIN
          --security-opt seccomp=unconfined --security-opt apparmor=unconfined
          --security-opt systempaths=unconfined
          -v "$harness":/opt/harness:ro -v "$harness/out":/out
          -e HARNESS_PORT="$port" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)")
    [ -t 1 ] && args+=(-t)
    [ "$mode" = shell ] && args+=(-i)
    if [ "$mode" = web ]; then
        # Published to this machine's loopback only, and no other port ever is. The number
        # is the same inside and outside because the page checks the Host header.
        args+=(-p "127.0.0.1:${port}:${port}")
        echo "The page's address with its token is printed below; open it in a browser on this machine."
    fi
    # No --network host anywhere: the recorder under test stays on the container's own network.
    "$docker" "${args[@]}" "$image" "$@"
}

case "$1" in
    build) shift; build_image "$@" ;;
    web)   ensure_image; run_container web web ;;
    shell) ensure_image; run_container shell shell ;;
    test)  ensure_image; run_container test test ;;
    size)
        "$docker" image inspect "$image" >/dev/null 2>&1 && echo "image $image: $(image_mb)" || echo "image $image: not built"
        "$docker" system df
        ;;
    prune)
        "$docker" builder prune -f >/dev/null 2>&1 || true
        "$docker" system df
        ;;
    clean)
        "$docker" rm -f "$name" >/dev/null 2>&1 || true
        "$docker" image rm "$image" >/dev/null 2>&1 || true
        "$docker" builder prune -f >/dev/null 2>&1 || true
        "$docker" system df
        echo "kept: $harness/out ($(du -sh "$harness/out" 2>/dev/null | cut -f1)); delete it yourself if you no longer want the reports"
        ;;
    *)     ensure_image; run_container suites "$@" ;;
esac
