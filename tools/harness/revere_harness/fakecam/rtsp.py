from __future__ import annotations

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstRtspServer", "1.0")
from gi.repository import GLib, Gst, GstRtspServer  # noqa: E402


SELECTOR = "sel"
SCENES = {"moving": "sink_0", "static": "sink_1", "scrolling": "sink_2", "noise": "sink_3"}


def launch_string(
    video: str,
    audio: str,
    width: int,
    height: int,
    fps: int,
    bitrate_kbps: int,
    scene: str,
) -> str:
    """Build a GStreamer launch string for the fake camera stream."""
    bits_per_second = bitrate_kbps * 1000

    # Both pictures feed a selector, so the scene can change while clients stay connected.
    caps = f"video/x-raw,width={width},height={height},framerate={fps}/1"
    moving = f"videotestsrc is-live=true pattern=ball background-color=0xff202020 ! {caps} ! sel.sink_0"
    still = f"videotestsrc is-live=true pattern=bar ! {caps} ! sel.sink_1"
    scrolling = f"videotestsrc is-live=true pattern=smpte horizontal-speed=12 ! {caps} ! sel.sink_2"
    noise = f"videotestsrc is-live=true pattern=snow ! {caps} ! sel.sink_3"
    pipeline_parts = [
        moving,
        still,
        scrolling,
        noise,
        f"input-selector name={SELECTOR} sync-streams=false",
        # The running-time stamp is small and changes every frame; tests read it back from recordings.
        "! timeoverlay halignment=left valignment=top font-desc=\"Sans 10\"",
        "! videoconvert ! video/x-raw,format=I420",
    ]

    if video == "h264":
        pipeline_parts.append(
            f"! openh264enc bitrate={bits_per_second} gop-size={fps} complexity=low "
            f"! h264parse config-interval=-1 ! rtph264pay name=pay0 pt=96"
        )
    elif video == "h265":
        pipeline_parts.append(
            f"! x265enc bitrate={bitrate_kbps} key-int-max={fps} tune=zerolatency "
            f"speed-preset=ultrafast ! h265parse config-interval=-1 ! rtph265pay name=pay0 pt=96"
        )

    if audio == "aac":
        pipeline_parts.append(
            "audiotestsrc is-live=true wave=ticks "
            "! audio/x-raw,rate=16000,channels=1 ! audioconvert "
            "! avenc_aac bitrate=32000 ! aacparse ! rtpmp4gpay name=pay1 pt=97"
        )
    elif audio == "mulaw":
        pipeline_parts.append(
            "audiotestsrc is-live=true wave=ticks "
            "! audio/x-raw,rate=8000,channels=1 ! mulawenc "
            "! rtppcmupay name=pay1 pt=0"
        )

    return "( " + " ".join(pipeline_parts) + " )"


class RtspServer:
    """One GStreamer RTSP server: a shared encoder per stream, optional basic auth."""

    ROLE = "user"

    def __init__(self, port: int, username: str, password: str) -> None:
        self._username = username
        self._password = password
        self._server = GstRtspServer.RTSPServer.new()
        self._server.set_address("0.0.0.0")
        self._server.set_service(str(port))
        self._source_id: int | None = None
        self._medias: list[GstRtspServer.RTSPMedia] = []
        self._scene = "moving"
        if username:
            token = GstRtspServer.RTSPToken.new()
            token.set_string("media.factory.role", self.ROLE)
            auth = GstRtspServer.RTSPAuth.new()
            auth.add_basic(GstRtspServer.RTSPAuth.make_basic(username, password), token)
            self._server.set_auth(auth)

    def add_stream(self, path: str, launch: str) -> None:
        factory = GstRtspServer.RTSPMediaFactory.new()
        factory.set_launch(launch)
        # Shared: one encoder per stream however many clients connect.
        factory.set_shared(True)
        if self._username:
            perms = GstRtspServer.RTSPPermissions.new()
            perms.add_role(self.ROLE)
            perms.add_permission_for_role(self.ROLE, "media.factory.access", True)
            perms.add_permission_for_role(self.ROLE, "media.factory.construct", True)
            factory.set_permissions(perms)
        factory.connect("media-configure", self._on_media_configure)
        self._server.get_mount_points().add_factory(path, factory)

    def start(self) -> None:
        if self._source_id is None:
            self._source_id = self._server.attach(None)

    def stop(self) -> None:
        if self._source_id is not None:
            GLib.source_remove(self._source_id)
            self._source_id = None
        # Close every client so recorders see the stream go away, not just stop.
        self._server.client_filter(lambda server, client, data: GstRtspServer.RTSPFilterResult.REMOVE, None)

    def stall(self, on: bool) -> None:
        state = Gst.State.PAUSED if on else Gst.State.PLAYING
        for media in list(self._medias):
            media.get_element().set_state(state)

    def set_scene(self, scene: str) -> None:
        """Switch every stream, now and for later clients, to "moving" or "static"."""
        if scene not in SCENES:
            raise ValueError(f"unknown scene {scene!r}; expected one of {sorted(SCENES)}")
        self._scene = scene
        for media in list(self._medias):
            self._apply_scene(media)

    def _apply_scene(self, media: GstRtspServer.RTSPMedia) -> None:
        selector = media.get_element().get_by_name(SELECTOR)
        if selector is not None:
            selector.set_property("active-pad", selector.get_static_pad(SCENES[self._scene]))

    def _on_media_configure(self, factory: GstRtspServer.RTSPMediaFactory, media: GstRtspServer.RTSPMedia) -> None:
        self._medias.append(media)
        self._apply_scene(media)
        media.connect("unprepared", self._on_unprepared)

    def _on_unprepared(self, media: GstRtspServer.RTSPMedia) -> None:
        if media in self._medias:
            self._medias.remove(media)
