# The recorder built for Linux, plus what the harness needs to run it.
# Build context: the repository root. Built for the machine's own architecture
# (arm64 on Apple silicon, amd64 elsewhere), so nothing is emulated.
ARG UBUNTU_VERSION=24.04

FROM ubuntu:${UBUNTU_VERSION} AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install --no-install-recommends -y \
        build-essential cmake ninja-build git pkg-config patchelf file ca-certificates curl zstd binutils python3 \
        libwebp-dev libopus-dev libopencv-dev \
        libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
        libgstrtspserver-1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
        libssl-dev libsqlite3-dev uuid-dev \
        libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
        libgl1-mesa-dev libglu1-mesa-dev libgtk-3-dev libayatana-appindicator3-dev libcairo2-dev \
        libasound2-dev libpulse-dev && \
    rm -rf /var/lib/apt/lists/*
COPY . /src
# REVERE_UI_AUTOMATION is the desktop app's test-only hook; a source tree without it ignores the setting.
RUN cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DREVERE_UI_AUTOMATION=ON -DSDL_PIPEWIRE=OFF && \
    cmake --build /build --parallel "$(nproc)" && \
    cmake --install /build --prefix /opt/stage && \
    test -x /opt/stage/revere/revere && \
    (find /opt/stage/revere -type f \( -name "*.so*" -o -name revere -o -name vision \) \
        -exec strip --strip-unneeded {} + 2>/dev/null || true) && \
    # The packages that hold the libraries the recorder was linked against, for the next stage.
    (for f in /opt/stage/revere/revere /opt/stage/revere/*.so* /opt/stage/revere/*/*.so*; do \
         LD_LIBRARY_PATH=/opt/stage/revere ldd "$f" 2>/dev/null; done \
        | awk '$3 ~ /^\// && $3 !~ /^\/opt\/stage/ {print $3}' | sort -u \
        | xargs -r -n1 readlink -f | xargs -r dpkg -S 2>/dev/null | cut -d: -f1 | sort -u \
        | tr '\n' ' ' > /opt/stage/runtime-packages.txt) && \
    # The build tree is not needed again; leaving it would only grow the build cache.
    rm -rf /build

FROM ubuntu:${UBUNTU_VERSION} AS run
ENV DEBIAN_FRONTEND=noninteractive PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 REVERE_HARNESS_CONTAINER=1
COPY --from=build /opt/stage/runtime-packages.txt /tmp/runtime-packages.txt
RUN apt-get update && \
    apt-get install --no-install-recommends -y \
        # the harness
        python3 python3-gi python3-rich python3-psutil python3-pytest \
        util-linux iproute2 iputils-ping procps rsync ffmpeg ca-certificates \
        # the fake cameras
        gir1.2-gstreamer-1.0 gir1.2-gst-rtsp-server-1.0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools \
        # gstreamer1.0-x holds the text overlay the cameras stamp their pictures with
        gstreamer1.0-x \
        # the desktop app offscreen
        libgtk-3-bin \
        # the libraries the recorder was linked against
        $(cat /tmp/runtime-packages.txt) && \
    rm -rf /var/lib/apt/lists/* /tmp/runtime-packages.txt
COPY --from=build /opt/stage/revere /opt/revere
# The harness itself is not in the image: it is mounted from the checkout at /opt/harness,
# entry script included, so editing it needs no rebuild.
WORKDIR /opt/harness
LABEL org.opencontainers.image.title="revere-harness"
ENTRYPOINT ["/opt/harness/container/entry.sh"]
