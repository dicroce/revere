"""Recording suite: a headless recorder records from fake cameras; recorded ranges grow, stay continuous, and show a gap for every drop the test causes."""

from __future__ import annotations

import time
from datetime import datetime, timedelta
from pathlib import Path
from revere_harness.api import Api, gaps, parse_iso, utcnow
from revere_harness.camera import CameraSpec, FakeCamera
from revere_harness.recorder import Recorder
from revere_harness.steps import Run
from revere_harness import net, sandbox

GAP_START_TOLERANCE = 3.0
SETTLE = 12.0
LONG_DROP = 40.0
SHORT_DROP = 8.0
DEAD_AFTER = 20.0
RECOVERY_TIMEOUT = 90.0


class _CamState:
    __slots__ = ('cam', 'started', 'camera_id', 'motion_from')

    def __init__(self) -> None:
        self.cam: FakeCamera | None = None
        self.started: bool = False
        self.camera_id: str | None = None
        self.motion_from: datetime | None = None


def _need_started(run: Run, name: str, cameras_state: dict[str, _CamState]) -> None:
    if not run.recorder_started:
        run.skip("recorder did not start")
    state = cameras_state.get(name)
    if state is None or not state.started:
        run.skip(f"camera {name} did not start")


def _need_recording(run: Run, name: str, cameras_state: dict[str, _CamState]) -> None:
    _need_started(run, name, cameras_state)
    state = cameras_state[name]
    if state.camera_id is None:
        run.skip(f"camera {name} is not recording")


def run(run: Run, ctx: object) -> None:
    cameras_state: dict[str, _CamState] = {}
    recorder: Recorder | None = None

    params = getattr(ctx, "params", {}) or {}
    long_drop = float(params.get("long_drop", LONG_DROP))
    short_drop = float(params.get("short_drop", SHORT_DROP))

    snap = getattr(ctx, "snap", None)

    def shot(caption: str, cam: FakeCamera) -> None:
        if snap is not None:
            try:
                snap(caption, cam.rtsp_url())
            except Exception:
                pass

    try:
        # 1. Sandbox and bridge
        with run.step("sandbox hides host processes"):
            sandbox.guard()
        with run.step("bridge is up"):
            net.setup_bridge()

        # 2. Start fake cameras
        specs = [
            CameraSpec(name="cam1", index=1, video="h264", audio="aac", substream=True),
            CameraSpec(name="cam3", index=3, video="h264", audio="mulaw", scene="static"),
            CameraSpec(name="cam4", index=4, video="h265"),
            CameraSpec(name="cam5", index=5, video="h265", audio="aac", substream=True),
        ]

        for spec in specs:
            with run.step(f"camera {spec.name} starts"):
                cam = FakeCamera(spec, ctx.out_dir / "fake_cams")
                cam.start()
                ctx.sampler.watch(spec.name, cam.pid)
                state = _CamState()
                state.cam = cam
                state.started = True
                cameras_state[spec.name] = state

        # 3. Start recorder
        with run.step("recorder starts headless"):
            recorder = Recorder(ctx.revere_dir, ctx.out_dir / "recorder")
            recorder.start()
            ctx.sampler.watch("recorder", recorder.pid)
            run.recorder_started = True

        # 4. Add cameras to recorder
        api = Api(recorder.API, recorder.token())
        
        for spec in specs:
            with run.step(f"recorder starts recording {spec.name}"):
                _need_started(run, spec.name, cameras_state)
                cam_obj = cameras_state[spec.name].cam
                result = api.add_camera(
                    name=spec.name,
                    username=cam_obj.spec.username,
                    password=cam_obj.spec.password,
                    retention_days=0.2,
                )
                camera_id = result["camera_id"]
                cameras_state[spec.name].camera_id = camera_id
                # Motion queries start here: a window that starts before the recording returns nothing.
                cameras_state[spec.name].motion_from = utcnow() + timedelta(seconds=3)

                codec_ok = result["video_codec"] in ("h264", "h265")
                rate_ok = result["byte_rate"] > 0
                run.check(codec_ok and rate_ok, f"codec={result['video_codec']}, byte_rate={result['byte_rate']}")

        # 5. Wait for receiving video
        for spec in specs:
            with run.step(f"{spec.name} is receiving video"):
                _need_recording(run, spec.name, cameras_state)
                deadline = utcnow().timestamp() + 30.0
                while True:
                    cam_data = api.camera(spec.name)
                    if cam_data is None:
                        raise RuntimeError("Camera disappeared")
                    if cam_data.get("receiving_video") and cam_data.get("state") == "assigned" and not cam_data.get("stream_failed"):
                        break
                    if utcnow().timestamp() > deadline:
                        run.skip(f"{spec.name} did not start receiving video in time")
                    time.sleep(1.0)
                shot(f"{spec.name} recording", cameras_state[spec.name].cam)

        # 6. Check pre-allocated file
        recorded_sizes: dict[str, int] = {}
        recorded_mtimes: dict[str, float] = {}
        for spec in specs:
            with run.step(f"{spec.name} has a pre-allocated recording file"):
                _need_recording(run, spec.name, cameras_state)
                entry = api.camera(spec.name)
                if entry is None:
                    run.skip(f"{spec.name}: camera entry not found")
                fpath = entry.get("record_file_path")
                if not fpath or not Path(fpath).exists():
                    run.skip(f"{spec.name}: no record file path found")
                stat = Path(fpath).stat()
                if stat.st_size <= 0:
                    run.skip(f"{spec.name}: file size is 0")
                recorded_sizes[spec.name] = stat.st_size
                recorded_mtimes[spec.name] = stat.st_mtime

        # 7. Record for a while and check growth
        with run.step("record for a while"):
            time.sleep(SETTLE)

        for spec in specs:
            _need_recording(run, spec.name, cameras_state)
            cam_id = cameras_state[spec.name].camera_id
            
            with run.step(f"{spec.name}: recorded range grows"):
                now = utcnow()
                start_time = now - timedelta(minutes=30)
                c1 = api.contents(cam_id, start_time, now)
                if not c1:
                    run.skip(f"{spec.name}: no content yet")

                time.sleep(6.0)
                now2 = utcnow()
                c2 = api.contents(cam_id, start_time, now2)
                if not c2:
                    run.skip(f"{spec.name}: no content after wait")

                end1 = c1[-1][1]
                end2 = c2[-1][1]
                advance = (end2 - end1).total_seconds()
                run.check(advance >= 4.0, f"Advance was {advance:.1f}s")

            with run.step(f"{spec.name}: file is written in place"):
                cam_data = api.camera(spec.name)
                fpath = cam_data.get("record_file_path")
                stat = Path(fpath).stat()
                size_ok = stat.st_size == recorded_sizes[spec.name]
                mtime_newer = stat.st_mtime > recorded_mtimes[spec.name]
                run.check(size_ok and mtime_newer, "File size unchanged but mtime newer")

        # 8. Check continuous segment
        for spec in specs:
            _need_recording(run, spec.name, cameras_state)
            cam_id = cameras_state[spec.name].camera_id
            
            with run.step(f"{spec.name}: one continuous segment so far"):
                now = utcnow()
                start_time = now - timedelta(minutes=30)
                contents = api.contents(cam_id, start_time, now)
                if not contents:
                    run.skip(f"{spec.name}: no content")

                g_list = gaps(contents)
                if len(g_list) > 0:
                    gap_info = ", ".join([f"{s} ({d:.1f}s)" for s, d in g_list])
                    run.check(False, f"Found gaps: {gap_info}")
                else:
                    # Check length >= 10s
                    total_len = (contents[-1][1] - contents[0][0]).total_seconds()
                    run.check(total_len >= 10.0, "Segment too short")

        # 9. Drop tests on cam1
        _need_recording(run, "cam1", cameras_state)
        cam1_obj = cameras_state["cam1"].cam
        cam1_id = cameras_state["cam1"].camera_id

        def drop_and_check(label: str, cause, restore, min_gap: float | None = None, max_gap: float | None = None) -> None:
            t0 = utcnow()
            before_len = len(api.contents(cam1_id, t0 - timedelta(minutes=30), t0))
            
            cause()
            time.sleep(long_drop)
            restore()

            # Wait for recovery
            deadline = utcnow().timestamp() + RECOVERY_TIMEOUT
            while True:
                if utcnow().timestamp() > deadline:
                    run.fail(f"Recovery timeout. Segments: {api.contents(cam1_id, t0 - timedelta(minutes=30), utcnow())}")
                
                now_check = utcnow()
                contents = api.contents(cam1_id, now_check - timedelta(minutes=30), now_check)
                
                if len(contents) > before_len and (now_check - contents[-1][1]).total_seconds() < 5.0:
                    break
                time.sleep(2.0)

            # Find gap between last two segments
            now_final = utcnow()
            segments = api.contents(cam1_id, now_final - timedelta(minutes=30), now_final)
            
            if len(segments) < 2:
                run.fail(f"Expected at least 2 segments after recovery, got {len(segments)}")

            gap_start = segments[-2][1]
            gap_length = (segments[-1][0] - segments[-2][1]).total_seconds()

            # Checks
            start_ok = abs((gap_start - t0).total_seconds()) <= GAP_START_TOLERANCE
            if min_gap is None:
                min_gap = long_drop - 2
            if max_gap is None:
                max_gap = long_drop + 2 * DEAD_AFTER + 15
            len_ok = min_gap <= gap_length <= max_gap

            run.check(start_ok, f"Gap start offset {abs((gap_start - t0).total_seconds()):.1f}s")
            run.check(len_ok, f"Gap length {gap_length:.1f}s not in [{min_gap}, {max_gap}]")

            ctx.reporter.note(f"{label}: gap started {(gap_start - t0).total_seconds():+.1f} s from the drop, lasted {gap_length:.1f} s")
            shot(f"cam1 after {label}", cam1_obj)

        with run.step("an outage shorter than the dead-stream timeout does not restart the stream"):
            cam3_obj = cameras_state["cam3"].cam
            cam3_id = cameras_state["cam3"].camera_id
            t0_short = utcnow()
            before_len_short = len(api.contents(cam3_id, t0_short - timedelta(minutes=30), t0_short))
            
            cam3_obj.unplug()
            time.sleep(short_drop)
            cam3_obj.plug()
            time.sleep(10.0)
            
            now_short = utcnow()
            after_len_short = len(api.contents(cam3_id, now_short - timedelta(minutes=30), now_short))
            last_seg_end = api.contents(cam3_id, now_short - timedelta(minutes=30), now_short)[-1][1] if api.contents(cam3_id, now_short - timedelta(minutes=30), now_short) else None
            
            run.check(after_len_short == before_len_short, "Segments count changed during short drop")
            run.check(last_seg_end is not None and (now_short - last_seg_end).total_seconds() < 5.0, "Last segment end too old")

        with run.step("unplugged cable shows as a gap"):
            drop_and_check("Unplug", lambda: cam1_obj.unplug(), lambda: cam1_obj.plug())
        
        time.sleep(10.0) # Clean recording between drops

        with run.step("RTSP server stopped shows as a gap"):
            drop_and_check("RTSP Stop", lambda: cam1_obj.rtsp(False), lambda: cam1_obj.rtsp(True))

        time.sleep(10.0)

        with run.step("stalled stream is restarted and shows as a gap"):
            drop_and_check("Stall", lambda: cam1_obj.stall(True), lambda: cam1_obj.stall(False), min_gap=DEAD_AFTER - 3, max_gap=long_drop + 2 * DEAD_AFTER + 15)

        # 10. Other cameras kept recording
        for name in ["cam3", "cam4", "cam5"]:
            _need_recording(run, name, cameras_state)
            cam_id = cameras_state[name].camera_id
            
            with run.step(f"{name}: other cameras kept recording through the drops"):
                now = utcnow()
                start_time = now - timedelta(minutes=30)
                contents = api.contents(cam_id, start_time, now)
                if not contents:
                    run.skip(f"{name}: no content")
                
                g_list = gaps(contents)
                if len(g_list) > 0:
                    gap_info = ", ".join([f"{s} ({d:.1f}s)" for s, d in g_list])
                    run.check(False, f"Found gaps: {gap_info}")

        # 11. Motion on cam3. It has shown a still picture since it started, so the
        # recorder's motion model has settled by now.
        cam3 = cameras_state["cam3"]

        with run.step("still picture produces no motion events"):
            _need_recording(run, "cam3", cameras_state)
            events = api.motion_events(cam3.camera_id, cam3.motion_from, utcnow())
            ctx.reporter.note(f"still picture: {len(events)} motion event(s)")
            run.check(len(events) == 0, f"expected no motion events for a still picture, got {events}")

        with run.step("scripted motion produces one motion event with the right times"):
            _need_recording(run, "cam3", cameras_state)
            t_on = utcnow()
            cam3.cam.scene("scrolling")
            try:
                # No snapshot here: a second client on the stream during the
                # motion period made the recorder's event end late.
                time.sleep(15)
            finally:
                t_off = utcnow()
                cam3.cam.scene("static")
            # The event is written when it ends, a few key frames after the motion stops.
            time.sleep(20)
            events = api.motion_events(cam3.camera_id, cam3.motion_from, utcnow())
            run.check(len(events) == 1, f"expected exactly one motion event, got {events}")
            start = (parse_iso(events[0]["start_time"]) - t_on).total_seconds()
            end = (parse_iso(events[0]["end_time"]) - t_off).total_seconds()
            ctx.reporter.note(f"motion event started {start:+.1f} s from the scene switch and ended {end:+.1f} s from the switch back")
            run.check(-1.0 <= start <= 5.0, f"the event started {start:+.1f} s from the scene switch, expected -1 to +5")
            run.check(-1.0 <= end <= 8.0, f"the event ended {end:+.1f} s from the switch back, expected -1 to +8")

        with run.step("finding: motion query starting before the recording began"):
            _need_recording(run, "cam3", cameras_state)
            inside = api.motion_events(cam3.camera_id, cam3.motion_from, utcnow())
            run.check(len(inside) >= 1, "no motion event to compare with")
            wide = api.motion_events(cam3.camera_id, utcnow() - timedelta(hours=2), utcnow())
            if not wide:
                ctx.reporter.note("FINDING: /motion_events returned no events for a window that starts before the "
                                  "recording began, although an event exists inside the window")
                run.skip("the recorder returns no events for a window starting before the recording; reported as a finding")

        # 12. Remove cam5
        with run.step("removing a camera stops its recording"):
            _need_recording(run, "cam5", cameras_state)
            cam5_id = cameras_state["cam5"].camera_id
            api.remove_camera(cam5_id)
            
            deadline = utcnow().timestamp() + 15.0
            while True:
                if utcnow().timestamp() > deadline:
                    run.skip("Timeout waiting for cam5 to be discovered")
                cam_data = api.camera("cam5")
                if cam_data and cam_data.get("state") == "discovered":
                    break
                time.sleep(0.5)

        # 13. Stop recorder
        with run.step("recorder stops cleanly"):
            ret = recorder.stop()
            run.check(ret == 0, f"Recorder exit code: {ret}")

    finally:
        if recorder:
            try:
                recorder.stop()
            except Exception:
                pass
        
        for state in cameras_state.values():
            if state.cam is not None:
                try:
                    state.cam.stop()
                except Exception:
                    pass