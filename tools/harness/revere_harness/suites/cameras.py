from __future__ import annotations

import time
from revere_harness import net, sandbox, probe
from revere_harness.camera import CameraSpec, FakeCamera
from revere_harness.steps import Run


def run(run: Run, ctx: object) -> None:
    """Camera suite: starts the fake camera matrix and proves that streams, discovery and every fault control work, without a recorder."""
    cameras: list[FakeCamera] = []

    try:
        with run.step("sandbox hides host processes"):
            sandbox.guard()

        with run.step("bridge is up"):
            net.setup_bridge()

        specs = [
            CameraSpec(name="cam1", index=1, video="h264", audio="none"),
            CameraSpec(name="cam2", index=2, video="h264", audio="aac", substream=True),
            CameraSpec(name="cam3", index=3, video="h264", audio="mulaw"),
            CameraSpec(name="cam4", index=4, video="h265", audio="none"),
            CameraSpec(name="cam5", index=5, video="h265", audio="aac", substream=True),
            CameraSpec(name="cam6", index=6, username="", password="", scene="static"),
        ]

        for spec in specs:
            with run.step(f"camera {spec.name} starts"):
                cam = FakeCamera(spec, ctx.out_dir / "cameras")
                cam.start()
                cameras.append(cam)
                ctx.sampler.watch(spec.name, cam.pid)

        if not cameras:
            return

        for cam in cameras:
            with run.step(f"camera {cam.spec.name} serves {cam.spec.video}[+{cam.spec.audio}]"):
                url = cam.rtsp_url(with_auth=cam.spec.username != "")
                streams = probe.ffprobe_streams(url)
                if not streams:
                    run.check(False, f"No streams for {url}")
                    continue

                video_streams = [s for s in streams if s.get("codec_type") == "video"]
                audio_streams = [s for s in streams if s.get("codec_type") == "audio"]

                run.check(len(video_streams) == 1, f"Expected 1 video stream, got {len(video_streams)}")
                vs = video_streams[0]
                codec = vs.get("codec_name", "")
                run.check(codec in ("h264", "hevc"), f"Unexpected video codec: {codec}")
                run.check(vs.get("width") == cam.spec.width, f"Width mismatch: {vs.get('width')} != {cam.spec.width}")
                run.check(vs.get("height") == cam.spec.height, f"Height mismatch: {vs.get('height')} != {cam.spec.height}")

                if cam.spec.audio != "none":
                    run.check(len(audio_streams) == 1, f"Expected 1 audio stream, got {len(audio_streams)}")
                    as_ = audio_streams[0]
                    expected_codec = "aac" if cam.spec.audio == "aac" else "pcm_mulaw"
                    run.check(as_.get("codec_name") == expected_codec, f"Unexpected audio codec: {as_.get('codec_name')}")
                else:
                    run.check(len(audio_streams) == 0, f"Expected no audio streams, got {len(audio_streams)}")

                if cam.spec.substream:
                    sub_url = cam.rtsp_url(stream="sub", with_auth=cam.spec.username != "")
                    sub_streams = probe.ffprobe_streams(sub_url)
                    sub_video = [s for s in sub_streams if s.get("codec_type") == "video"]
                    run.check(len(sub_video) == 1, f"Substream video count mismatch: {len(sub_video)}")
                    sv = sub_video[0]
                    run.check(sv.get("width") == 640, f"Substream width mismatch: {sv.get('width')}")
                    run.check(sv.get("height") == 360, f"Substream height mismatch: {sv.get('height')}")

        with run.step("RTSP refuses a wrong password"):
            cam1 = cameras[0]
            wrong_url = cam1.rtsp_url(with_auth=True).replace(f"{cam1.spec.password}", "wrong")
            streams = probe.ffprobe_streams(wrong_url)
            run.check(len(streams) == 0, f"Expected no streams with wrong password, got {len(streams)}")

        with run.step("RTSP without auth is open"):
            cam6 = cameras[5]
            url_no_auth = cam6.rtsp_url(with_auth=False)
            streams = probe.ffprobe_streams(url_no_auth)
            run.check(len(streams) > 0, f"Expected streams without auth, got {len(streams)}")

        with run.step("multicast discovery finds every camera"):
            replies = probe.onvif_probe(expect=len(cameras))
            found_ips = {r["from"] for r in replies}
            expected_ips = {cam.ip for cam in cameras}
            run.check(found_ips == expected_ips, f"Discovery IPs mismatch: {found_ips} != {expected_ips}")
            for reply in replies:
                cam_ip = reply["from"]
                matching_cam = next((c for c in cameras if c.ip == cam_ip), None)
                if matching_cam:
                    run.check(matching_cam.ip in reply["xaddrs"], f"xaddrs missing IP for {cam_ip}")

        with run.step("unicast discovery finds a camera"):
            cam1 = cameras[0]
            replies = probe.onvif_probe(target=cam1.ip, expect=1)
            if replies:
                run.check(replies[0]["from"] == cam1.ip, f"Reply from wrong IP: {replies[0]['from']}")

        # Fault self-tests on cam1 (index 0)
        cam1 = cameras[0]

        with run.step("unplug makes the stream unreachable, plug restores it"):
            url = cam1.rtsp_url()
            cam1.unplug()
            streams_unplugged = probe.ffprobe_streams(url)
            run.check(len(streams_unplugged) == 0, "Stream should be unreachable when unplugged")
            cam1.plug()
            streams_after = probe.ffprobe_streams(url)
            run.check(len(streams_after) > 0, "Stream should be reachable after plug")

        with run.step("RTSP off refuses connections, RTSP on restores them"):
            url = cam1.rtsp_url()
            cam1.rtsp(on=False)
            streams_off = probe.ffprobe_streams(url)
            run.check(len(streams_off) == 0, "Stream should be unreachable when RTSP is off")
            cam1.rtsp(on=True)
            streams_on = probe.ffprobe_streams(url)
            run.check(len(streams_on) > 0, "Stream should be reachable after RTSP on")

        with run.step("ONVIF off silences discovery, ONVIF on restores it"):
            cam1.onvif(on=False)
            replies_off = probe.onvif_probe(target=cam1.ip, timeout=1.5)
            run.check(len(replies_off) == 0, "Discovery should return 0 when ONVIF is off")
            cam1.onvif(on=True)
            replies_on = probe.onvif_probe(target=cam1.ip, expect=1)
            if replies_on:
                run.check(replies_on[0]["from"] == cam1.ip, "Discovery should return 1 after ONVIF on")

        with run.step("IP change: old address silent, new address answers"):
            old_ip = cam1.ip
            new_ip = "10.77.0.201"
            cam1.set_ip(new_ip)
            
            replies_old = probe.onvif_probe(target=old_ip, timeout=1.5)
            run.check(len(replies_old) == 0, f"Discovery should return 0 for old IP {old_ip}")
            
            replies_new = probe.onvif_probe(target=new_ip, expect=1)
            if replies_new:
                run.check(new_ip in replies_new[0]["xaddrs"], f"xaddrs should contain new IP {new_ip}")

            new_url = cam1.rtsp_url().replace(old_ip, new_ip)
            streams_new = probe.ffprobe_streams(new_url)
            run.check(len(streams_new) > 0, "Stream should be reachable on new IP")

            cam1.set_ip(old_ip)
            replies_back = probe.onvif_probe(target=old_ip, expect=1)
            if replies_back:
                run.check(replies_back[0]["from"] == old_ip, "Discovery should return 1 after restoring IP")

        with run.step("MAC change is visible in the neighbour table"):
            old_mac = cam1.mac
            new_mac = "02:00:00:00:ee:01"
            cam1.set_mac(new_mac)
            
            net.flush_neighbours()
            probe.onvif_probe(target=cam1.ip, expect=1)
            
            time.sleep(0.5)
            
            arp_mac = net.arp_mac(cam1.ip)
            run.check(arp_mac == new_mac.lower(), f"ARP MAC mismatch: {arp_mac} != {new_mac}")

            cam1.set_mac(old_mac)
            net.flush_neighbours()

        with run.step("slow link adds the configured delay"):
            cam1.slow(delay_ms=300)
            replies = probe.onvif_probe(target=cam1.ip, expect=1)
            run.check(len(replies) == 1, "Expected exactly one reply")
            if replies:
                run.check(replies[0]["seconds"] >= 0.25, f"Delay too short: {replies[0]['seconds']}s")
            
            cam1.normal()
            replies = probe.onvif_probe(target=cam1.ip, expect=1)
            run.check(len(replies) == 1, "Expected exactly one reply")
            if replies:
                run.check(replies[0]["seconds"] < 0.2, f"Normal delay too long: {replies[0]['seconds']}s")

        with run.step("scene switch: still picture has no motion, moving picture has"):
            cam1.scene("static")
            time.sleep(0.5)
            still = probe.picture_change(cam1.rtsp_url())
            cam1.scene("moving")
            time.sleep(0.5)
            moving = probe.picture_change(cam1.rtsp_url())
            run.check(still is not None and moving is not None, "Picture change probes returned None")
            run.check(still < 0.05, f"Still picture motion too high: {still}")
            run.check(moving > 0.2, f"Moving picture motion too low: {moving}")

        with run.step("stall keeps the session open but stops data"):
            run.skip(reason="checked in the recording suite")

    finally:
        for cam in cameras:
            try:
                cam.stop()
            except Exception:
                pass
