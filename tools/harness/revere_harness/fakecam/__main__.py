import json
import sys
import threading
import gi

gi.require_version("Gst", "1.0")

from gi.repository import GLib, Gst

from revere_harness.fakecam.rtsp import RtspServer, launch_string
from revere_harness.fakecam.onvif import OnvifConfig, OnvifResponder


def main() -> None:
    config_str = sys.argv[1]
    cfg = json.loads(config_str)

    name = cfg["name"]
    serial = cfg["serial"]
    manufacturer = cfg["manufacturer"]
    model = cfg["model"]
    username = cfg.get("username", "")
    password = cfg.get("password", "")
    rtsp_port = cfg["rtsp_port"]
    onvif_port = cfg["onvif_port"]
    video_codec = cfg["video"]
    audio_codec = cfg["audio"]
    width = cfg["width"]
    height = cfg["height"]
    fps = cfg["fps"]
    bitrate_kbps = cfg["bitrate_kbps"]
    substream = cfg.get("substream", False)
    scene = cfg.get("scene", "moving")

    profiles = []
    main_profile = {
        "token": "main",
        "path": "/main",
        "encoding": video_codec.upper(),
        "width": width,
        "height": height,
        "fps": fps,
        "bitrate_kbps": bitrate_kbps,
    }
    profiles.append(main_profile)

    if substream:
        sub_profile = {
            "token": "sub",
            "path": "/sub",
            "encoding": video_codec.upper(),
            "width": 640,
            "height": 360,
            "fps": 30,
            "bitrate_kbps": 300,
        }
        profiles.append(sub_profile)

    onvif_config = OnvifConfig(
        name=name,
        serial=serial,
        manufacturer=manufacturer,
        model=model,
        username=username,
        password=password,
        onvif_port=onvif_port,
        rtsp_port=rtsp_port,
        profiles=profiles,
    )

    Gst.init(None)

    rtsp_server = RtspServer(port=rtsp_port, username=username, password=password)
    for p in profiles:
        # The substream carries video only, as on most cameras.
        stream_audio = audio_codec if p["token"] == "main" else "none"
        launch = launch_string(
            video_codec, stream_audio, p["width"], p["height"], p["fps"], p["bitrate_kbps"], scene
        )
        rtsp_server.add_stream(p["path"], launch)

    rtsp_server.set_scene(scene)

    onvif_responder = OnvifResponder(config=onvif_config, log=lambda msg: print(f"[{name}] {msg}", file=sys.stderr))

    started_rtsp = False
    started_onvif = False
    stalled = False

    loop = GLib.MainLoop()

    def handle_command(cmd_obj: dict) -> None:
        nonlocal started_rtsp, started_onvif, stalled
        cmd = cmd_obj.get("cmd")
        try:
            if cmd == "start":
                rtsp_server.start()
                onvif_responder.start()
                started_rtsp = True
                started_onvif = True
                reply = {"ok": True, "cmd": "start"}
            elif cmd == "rtsp":
                on = cmd_obj.get("on", False)
                if on:
                    rtsp_server.start()
                    started_rtsp = True
                else:
                    rtsp_server.stop()
                    started_rtsp = False
                reply = {"ok": True, "cmd": "rtsp"}
            elif cmd == "onvif":
                on = cmd_obj.get("on", False)
                if on:
                    onvif_responder.start()
                    started_onvif = True
                else:
                    onvif_responder.stop()
                    started_onvif = False
                reply = {"ok": True, "cmd": "onvif"}
            elif cmd == "stall":
                on = cmd_obj.get("on", False)
                rtsp_server.stall(on)
                stalled = on
                reply = {"ok": True, "cmd": "stall"}
            elif cmd == "scene":
                rtsp_server.set_scene(cmd_obj.get("name", "moving"))
                reply = {"ok": True, "cmd": "scene"}
            elif cmd == "seen":
                reply = {"ok": True, "cmd": "seen", "requests": onvif_responder.requests_seen}
            elif cmd == "quit":
                rtsp_server.stop()
                onvif_responder.stop()
                print(json.dumps({"ok": True, "cmd": "quit"}), flush=True)
                loop.quit()
                return
            else:
                reply = {"ok": False, "cmd": cmd, "error": f"Unknown command: {cmd}"}

            print(json.dumps(reply), flush=True)
        except Exception as e:
            error_msg = f"{type(e).__name__}: {e}"
            print(json.dumps({"ok": False, "cmd": cmd, "error": error_msg}), flush=True)

    def stdin_thread() -> None:
        for line in sys.stdin:
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            GLib.idle_add(handle_command, obj)

    t = threading.Thread(target=stdin_thread, daemon=True)
    t.start()

    print(json.dumps({"event": "ready"}), flush=True)

    loop.run()
    sys.exit(0)


if __name__ == "__main__":
    main()