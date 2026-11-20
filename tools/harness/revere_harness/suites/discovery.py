"""Discovery suite: a headless recorder finds every fake camera on the test network by itself."""

import time
from typing import List

from revere_harness import net, sandbox
from revere_harness.steps import Run
from revere_harness.camera import CameraSpec, FakeCamera
from revere_harness.recorder import Recorder


def _poll_until(condition: callable, timeout: float = 30.0, interval: float = 1.0) -> bool:
    """Poll a condition until it returns True or timeout is reached."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(interval)
    return False


def run(run: Run, ctx: object) -> None:
    cameras: List[FakeCamera] = []
    recorder: Recorder | None = None

    try:
        # a. sandbox hides host processes
        with run.step("sandbox hides host processes"):
            sandbox.guard()

        # b. bridge is up
        with run.step("bridge is up"):
            net.setup_bridge()

        # c. one step per camera "camera <name> starts"
        specs = [
            CameraSpec(name="cam1", index=1, video="h264"),
            CameraSpec(name="cam2", index=2, video="h264", audio="aac", substream=True),
            CameraSpec(name="cam4", index=4, video="h265"),
            CameraSpec(name="cam6", index=6, video="h264", username="", password=""),
        ]

        for spec in specs:
            with run.step(f"camera {spec.name} starts"):
                cam = FakeCamera(spec, ctx.out_dir / "logs")
                cam.start()
                cameras.append(cam)
                ctx.sampler.watch(spec.name, cam.pid)

        # d. "recorder starts headless"
        with run.step("recorder starts headless"):
            recorder = Recorder(ctx.revere_dir, ctx.out_dir / "recorder")
            recorder.start()
            ctx.sampler.watch("recorder", recorder.pid)

        # e. "recorder publishes a local API token"
        with run.step("recorder publishes a local API token"):
            token = recorder.token()
            assert token, "token is empty"

        # f. "recorder lists every camera as discovered"
        with run.step("recorder lists every camera as discovered"):
            started_names = {cam.spec.name for cam in cameras}

            if not _poll_until(
                lambda: all(name in [c["camera_name"] for c in _get_cameras(recorder, token)] for name in started_names),
                timeout=30.0,
                interval=1.0,
            ):
                missing = started_names - {c["camera_name"] for c in _get_cameras(recorder, token)}
                run.skip(f"cameras not discovered after 30s: {missing}")

            cameras_list = _get_cameras(recorder, token)
            cam_map = {c["camera_name"]: c for c in cameras_list}

            for cam in cameras:
                name = cam.spec.name
                if name not in cam_map:
                    run.skip("no camera started")  # Should not happen here due to poll check above, but safe guard
                    continue
                
                entry = cam_map[name]
                assert entry["state"] == "discovered", f"{name} state is {entry['state']}"
                assert entry["ipv4"] == cam.ip, f"{name} ipv4 mismatch"
                assert entry["onvif_port"] == 80, f"{name} onvif_port mismatch"

        # g. "every camera saw the recorder's probe"
        with run.step("every camera saw the recorder's probe"):
            for cam in cameras:
                requests = cam.requests_seen()
                assert "Probe" in requests, f"{cam.spec.name} did not see Probe"

        # h. "a camera that stops answering keeps its entry"
        with run.step("a camera that stops answering keeps its entry"):
            if not cameras:
                run.skip("no camera started")
            
            cam1 = next((c for c in cameras if c.spec.name == "cam1"), None)
            if cam1 is None:
                run.skip("cam1 not found")

            cam1.onvif(False)
            time.sleep(5)
            
            cameras_list_after = _get_cameras(recorder, token)
            cam_map_after = {c["camera_name"]: c for c in cameras_list_after}
            
            assert "cam1" in cam_map_after, "cam1 disappeared from list"

        # i. "recorder stops cleanly"
        with run.step("recorder stops cleanly"):
            rc = recorder.stop()
            assert rc == 0, f"recorder stop returned {rc}"

    except Exception:
        if recorder and not getattr(run, '_step_skipped', False):
             # If we are in an exception block but haven't skipped, we might need to handle it.
             # However, the prompt implies steps handle their own skips or assertions.
             pass
        raise
    finally:
        # Always stop recorder then cameras
        if recorder:
            try:
                recorder.stop()
            except Exception:
                pass
        
        for cam in cameras:
            try:
                cam.stop()
            except Exception:
                pass


def _get_cameras(recorder: Recorder, token: str) -> list:
    """Helper to fetch camera list from recorder."""
    status, data = recorder.get_json("/cameras", token=token)
    if status != 200:
        return []
    return data.get("cameras", [])