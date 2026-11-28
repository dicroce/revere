"""Stress suite: one scenario per run, chosen by parameter.

Each scenario pushes the recorder in one way, checks that it keeps working, and
flags growth in its memory, threads and file descriptors.
"""
from __future__ import annotations

import shutil
import subprocess
import threading
import time
from datetime import timedelta

from revere_harness import growth, net, sandbox
from revere_harness.api import Api, ApiError, utcnow
from revere_harness.camera import CameraSpec, FakeCamera
from revere_harness.recorder import Recorder
from revere_harness.steps import Run

STORM_DOWN = 30.0        # longer than the recorder's dead-stream timeout, so every stream restarts
RECOVERY_TIMEOUT = 120.0
DISK_MB = 96


class _Rig:
    """The cameras and recorder of one run, and what is known about them."""

    def __init__(self, run: Run, ctx: object):
        self.run = run
        self.ctx = ctx
        self.cams: list[FakeCamera] = []
        self.recorder: Recorder | None = None
        self.api: Api | None = None
        self.ids: dict[str, str] = {}
        # When the scenario's load began, on the sampler's clock; growth is judged from here, not from the set-up.
        self.load_started: float | None = None
        self.growth_min_seconds = 75.0

    @property
    def names(self) -> list[str]:
        return list(self.ids)

    def need_recorder(self) -> None:
        if self.api is None:
            self.run.skip("recorder did not start")

    def need_recording(self) -> None:
        self.need_recorder()
        if not self.ids:
            self.run.skip("no camera is recording")

    def start_cameras(self, count: int) -> None:
        params = self.ctx.params
        width, height = (int(v) for v in params["resolution"].split("x"))
        with self.run.step(f"start {count} cameras"):
            failed = []
            for index in range(1, count + 1):
                spec = CameraSpec(name=f"cam{index:03d}", index=index,
                                  video="h264" if index % 2 else "h265",
                                  width=width, height=height, fps=params["fps"],
                                  bitrate_kbps=params["bitrate_kbps"])
                cam = FakeCamera(spec, self.ctx.out_dir / "cameras")
                try:
                    cam.start()
                    self.cams.append(cam)
                except Exception as exc:
                    failed.append(f"{spec.name}: {exc}")
            if self.cams:
                # One camera is enough to see what a fake costs; sampling all of them adds noise.
                self.ctx.sampler.watch("camera", self.cams[0].pid)
            self.run.check(not failed, f"{len(self.cams)} of {count} cameras started; failures: {failed[:5]}")

    def start_recorder(self, small_disk: bool = False) -> None:
        recorder = Recorder(self.ctx.revere_dir, self.ctx.out_dir / "recorder")
        if small_disk:
            with self.run.step(f"mount a {DISK_MB} MB data directory"):
                recorder.data_dir.mkdir(parents=True, exist_ok=True)
                subprocess.run(["mount", "-t", "tmpfs", "-o", f"size={DISK_MB}m", "tmpfs", str(recorder.data_dir)],
                               check=True, capture_output=True, text=True)
            # Without the small disk every camera would pre-allocate its file on the real one.
            if self.run.results[-1].status != "passed":
                return
        with self.run.step("recorder starts headless"):
            if small_disk:
                total_mb = shutil.disk_usage(recorder.data_dir).total / 2**20
                self.run.check(0.9 * DISK_MB <= total_mb <= 1.1 * DISK_MB,
                               f"the data directory holds {total_mb:.0f} MB, not the {DISK_MB} MB test disk; "
                               "the recorder was not started")
            recorder.start()
            self.recorder = recorder
            self.ctx.sampler.watch("recorder", recorder.pid)
            self.api = Api(recorder.API, recorder.token())

    def add_all(self, retention_days: float = 0.1) -> None:
        with self.run.step(f"recorder starts recording {len(self.cams)} cameras"):
            self.need_recorder()
            failed = []
            for cam in self.cams:
                spec = cam.spec
                try:
                    self.ids[spec.name] = self.api.add_camera(spec.name, spec.username, spec.password,
                                                              retention_days=retention_days)["camera_id"]
                except ApiError as exc:
                    failed.append(f"{spec.name}: {exc}")
            self.run.check(not failed, f"{len(self.ids)} of {len(self.cams)} cameras added; failures: {failed[:5]}")

    def wait_receiving(self, names: list[str], timeout: float) -> tuple[list[str], float]:
        """Returns (cameras still not receiving, seconds waited)."""
        started = time.monotonic()
        while True:
            by_name = {c["camera_name"]: c for c in self.api.cameras()}
            missing = [n for n in names if not by_name.get(n, {}).get("receiving_video")]
            waited = time.monotonic() - started
            if not missing or waited >= timeout:
                return missing, waited
            time.sleep(2)

    def wait_recording(self, names: list[str], timeout: float) -> tuple[list[str], float]:
        """Returns (cameras whose recording is not advancing, seconds waited).

        The camera list can say a camera is receiving video while its stream is still
        being restarted, so this looks at what was actually recorded: the newest
        segment must end within the last few seconds.
        """
        started = time.monotonic()
        while True:
            now = utcnow()
            stale = []
            for name in names:
                segments = self.api.contents(self.ids[name], now - timedelta(hours=2), now)
                if not segments or (now - segments[-1][1]).total_seconds() > 5:
                    stale.append(name)
            waited = time.monotonic() - started
            if not stale or waited >= timeout:
                return stale, waited
            time.sleep(2)

    def all_receiving(self) -> None:
        with self.run.step("all cameras are receiving video"):
            self.need_recording()
            missing, waited = self.wait_receiving(self.names, RECOVERY_TIMEOUT)
            self.run.check(not missing, f"not receiving after {waited:.0f} s: {missing}")
            self.ctx.reporter.note(f"all {len(self.names)} cameras receiving after {waited:.0f} s")

    def health(self) -> dict:
        by_name = {c["camera_name"]: c for c in self.api.cameras()}
        entries = [by_name.get(n, {}) for n in self.names]
        return {
            "not_receiving": [n for n, c in zip(self.names, entries) if not c.get("receiving_video")],
            "failed": [n for n, c in zip(self.names, entries) if c.get("stream_failed")],
            "lagging": [n for n, c in zip(self.names, entries) if (c.get("ingest_lag") or {}).get("lagging")],
            "max_lag_ms": max([(c.get("ingest_lag") or {}).get("lag_ms", 0) for c in entries] or [0]),
        }

    def finish(self) -> None:
        """The checks every scenario ends with."""
        with self.run.step("recorder is still running"):
            self.need_recorder()
            self.run.check(self.recorder.proc.poll() is None,
                           f"the recorder exited with code {self.recorder.proc.returncode}")
        with self.run.step("no resource growth in the recorder"):
            self.need_recorder()
            # A short run is still warming up; its second half says nothing about growth.
            results = growth.analyse(self.ctx.sampler.rows("recorder"), min_seconds=self.growth_min_seconds, since=self.load_started)
            if not results:
                self.run.skip(f"run too short to judge growth (needs at least {2 * self.growth_min_seconds:.0f} s of load)")
            flagged = []
            for result in results:
                text = growth.describe("recorder", result)
                if result["flagged"]:
                    flagged.append(text)
                    self.ctx.reporter.note("FINDING: " + text)
                else:
                    self.ctx.reporter.note(text)
            self.run.check(not flagged, "; ".join(flagged))
        with self.run.step("recorder stops cleanly"):
            self.need_recorder()
            code = self.recorder.stop()
            self.run.check(code == 0, f"the recorder exited with code {code}")

    def stop_everything(self) -> None:
        if self.recorder is not None:
            try:
                self.recorder.stop()
            except Exception:
                pass
        for cam in self.cams:
            try:
                cam.stop()
            except Exception:
                pass


def run(run: Run, ctx: object) -> None:
    params = ctx.params
    scenario = params["scenario"]
    seconds = float(params["minutes"]) * 60
    rig = _Rig(run, ctx)
    try:
        # A step records its failure instead of raising, so each of these is checked before going on:
        # nothing below may run outside the sandbox or without its own network.
        with run.step("sandbox hides host processes"):
            sandbox.guard()
        if run.results[-1].status != "passed":
            return
        with run.step("bridge is up"):
            net.setup_bridge()
        if run.results[-1].status != "passed":
            return
        ctx.reporter.note(f"scenario {scenario}: {params}")

        if scenario in ("many_cameras", "soak"):
            rig.start_cameras(params["cameras"])
            rig.start_recorder()
            rig.add_all()
            rig.all_receiving()
            _stay_healthy(rig, seconds)
        elif scenario == "reconnect_storm":
            rig.start_cameras(params["cameras"])
            rig.start_recorder()
            rig.add_all()
            rig.all_receiving()
            _reconnect_storm(rig, seconds)
        elif scenario == "add_remove":
            rig.start_cameras(params["cameras"])
            rig.start_recorder()
            _add_remove(rig, seconds)
        elif scenario == "api_hammer":
            rig.start_cameras(2)
            rig.start_recorder()
            rig.add_all()
            rig.all_receiving()
            _api_hammer(rig, seconds, params["clients"])
        elif scenario == "disk_full":
            rig.start_cameras(max(3, params["cameras"]))
            rig.start_recorder(small_disk=True)
            _disk_full(rig)
        rig.finish()
    finally:
        rig.stop_everything()


def _stay_healthy(rig: _Rig, seconds: float) -> None:
    run, ctx = rig.run, rig.ctx
    with run.step(f"recording stays healthy for {seconds / 60:g} minutes"):
        rig.need_recording()
        started = time.monotonic()
        rig.load_started = rig.ctx.sampler.elapsed()
        checks, bad, lagging_checks, worst_lag, snapped, last_note = 0, {}, 0, 0, False, 0.0
        while time.monotonic() - started < seconds:
            time.sleep(min(15.0, max(0.0, seconds - (time.monotonic() - started))))
            state = rig.health()
            checks += 1
            for name in state["not_receiving"] + state["failed"]:
                bad[name] = bad.get(name, 0) + 1
            lagging_checks += 1 if state["lagging"] else 0
            worst_lag = max(worst_lag, state["max_lag_ms"])
            elapsed = time.monotonic() - started
            if elapsed - last_note >= 60:
                last_note = elapsed
                ctx.reporter.note(f"{elapsed / 60:.1f} min: {len(rig.names) - len(state['not_receiving'])} of "
                                  f"{len(rig.names)} receiving, {len(state['failed'])} failed, "
                                  f"{len(state['lagging'])} lagging, largest lag {state['max_lag_ms']} ms")
            if not snapped and elapsed >= seconds / 2:
                snapped = True
                # From the recorder, not the fake camera: a second RTSP client on it resets its timeline.
                try:
                    entry = rig.api.camera(rig.cams[0].spec.name)
                    data = rig.api.picture(entry["id"]) if entry and entry.get("id") else None
                    if data:
                        ctx.snap_jpeg(f"{rig.cams[0].spec.name} halfway", data)
                except Exception:
                    pass
        ctx.reporter.note(f"{checks} health checks; {lagging_checks} showed a lagging camera; largest lag {worst_lag} ms")
        run.check(not bad, f"cameras not receiving or failed, with the number of checks: {bad}")


def _reconnect_storm(rig: _Rig, seconds: float) -> None:
    run, ctx = rig.run, rig.ctx
    with run.step("reconnect storm: every camera dropped and returned together"):
        rig.need_recording()
        started = time.monotonic()
        rig.load_started = rig.ctx.sampler.elapsed()
        # Memory settles over the first ten minutes of a storm; a shorter one cannot tell that from growth.
        rig.growth_min_seconds = 300.0
        cycles = []
        drops = []
        while time.monotonic() - started < seconds:
            drops.append(utcnow())
            for cam in rig.cams:
                cam.unplug()
            time.sleep(STORM_DOWN)
            for cam in rig.cams:
                cam.plug()
            missing, waited = rig.wait_recording(rig.names, RECOVERY_TIMEOUT)
            cycles.append((waited, missing))
            # A stretch of clean recording between cycles, so each one stands alone.
            time.sleep(10)
            ctx.reporter.note(f"cycle {len(cycles)}: all back after {waited:.0f} s" if not missing
                              else f"cycle {len(cycles)}: still missing after {waited:.0f} s: {missing}")
        slowest = max(w for w, _ in cycles)
        ctx.reporter.note(f"{len(cycles)} cycles; slowest recovery {slowest:.0f} s")
        failed = {i + 1: m for i, (_, m) in enumerate(cycles) if m}
        run.check(not failed, f"cameras did not come back, by cycle: {failed}")
    with run.step("every camera shows one gap per storm cycle"):
        rig.need_recording()
        wrong = {}
        overwritten = 0
        for name, camera_id in rig.ids.items():
            segments = rig.api.contents(camera_id, utcnow() - timedelta(hours=2), utcnow())
            if not segments:
                wrong[name] = "nothing recorded"
                continue
            # A recording is a ring: in a long storm the oldest video, and its gaps, are overwritten.
            # Only the drops that happened after the oldest video still on disk can show.
            expected = sum(1 for drop in drops if drop > segments[0][0])
            overwritten = max(overwritten, len(drops) - expected)
            if len(segments) - 1 != expected:
                wrong[name] = f"{len(segments) - 1} of {expected}"
        if overwritten:
            ctx.reporter.note(f"up to {overwritten} of {len(drops)} drops were before the oldest video still recorded")
        run.check(not wrong, f"gaps found of gaps expected, by camera: {wrong}")


def _add_remove(rig: _Rig, seconds: float) -> None:
    run, ctx = rig.run, rig.ctx
    with run.step("add and remove cameras repeatedly"):
        rig.need_recorder()
        started = time.monotonic()
        rig.load_started = rig.ctx.sampler.elapsed()
        cycles, adds, removes, errors = 0, 0, 0, []
        while time.monotonic() - started < seconds:
            cycles += 1
            ids = {}
            for cam in rig.cams:
                spec = cam.spec
                try:
                    ids[spec.name] = rig.api.add_camera(spec.name, spec.username, spec.password,
                                                        retention_days=0.02)["camera_id"]
                    adds += 1
                except ApiError as exc:
                    errors.append(f"cycle {cycles} add {spec.name}: {exc}")
            for name, camera_id in ids.items():
                try:
                    rig.api.remove_camera(camera_id, delete_files=True)
                    removes += 1
                except ApiError as exc:
                    errors.append(f"cycle {cycles} remove {name}: {exc}")
        ctx.reporter.note(f"{cycles} cycles, {adds} adds, {removes} removes, {len(errors)} errors")
        run.check(not errors, f"{len(errors)} errors; first: {errors[:5]}")
    with run.step("no camera is left assigned"):
        rig.need_recorder()
        left = [c["camera_name"] for c in rig.api.cameras() if c.get("state") == "assigned"]
        run.check(not left, f"still assigned: {left}")


def _api_hammer(rig: _Rig, seconds: float, clients: int) -> None:
    run, ctx = rig.run, rig.ctx
    with run.step(f"hammer the API with {clients} clients"):
        rig.need_recording()
        api = rig.api
        wrong = Api(rig.recorder.API, "wrong-token")
        camera_id = next(iter(rig.ids.values()))

        def window() -> dict:
            now = utcnow()
            return {"camera_id": camera_id, "start_time": _iso(now - timedelta(minutes=10)), "end_time": _iso(now)}

        # label, expected status (None: any error status is fine), request
        requests = [
            ("GET /cameras", 200, lambda: api.request("GET", "/cameras")),
            ("GET /version", 200, lambda: api.request("GET", "/version")),
            ("GET /contents", 200, lambda: api.request("GET", "/contents", query=window())),
            ("GET /contents, no parameters", None, lambda: api.request("GET", "/contents")),
            ("GET /cameras, wrong token", 401, lambda: wrong.request("GET", "/cameras")),
            ("GET /motion_events, unknown camera", None,
             lambda: api.request("GET", "/motion_events", query={**window(), "camera_id": "no-such-camera"})),
            ("POST /configure_camera, empty body", None, lambda: api.request("POST", "/configure_camera", body={})),
        ]
        counts: dict[tuple[str, int], int] = {}
        lock = threading.Lock()
        deadline = time.monotonic() + seconds
        rig.load_started = rig.ctx.sampler.elapsed()

        def client(offset: int) -> None:
            i = offset
            while time.monotonic() < deadline:
                label, _, call = requests[i % len(requests)]
                i += 1
                try:
                    status = call()[0]
                except Exception:
                    status = 0
                with lock:
                    counts[(label, status)] = counts.get((label, status), 0) + 1

        threads = [threading.Thread(target=client, args=(n,), daemon=True) for n in range(clients)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(seconds + 120)

        total = sum(counts.values())
        ctx.reporter.note(f"{total} requests, {total / seconds:.0f} a second")
        problems = []
        for label, expected, _ in requests:
            seen = {status: n for (name, status), n in sorted(counts.items()) if name == label}
            ctx.reporter.note(f"{label}: {seen}")
            for status, n in seen.items():
                if status == 0:
                    problems.append(f"{label}: {n} requests got no answer")
                elif expected is not None and status != expected:
                    problems.append(f"{label}: {n} answers were {status}, expected {expected}")
                elif expected is None and status < 400:
                    # Not this scenario's subject: report it and carry on.
                    ctx.reporter.note(f"FINDING: {label} was answered with {status}, not an error status")
                elif expected is None and status >= 500:
                    ctx.reporter.note(f"FINDING: {label} (a client mistake) was answered with {status}, not a 4xx status")
        run.check(not problems, "; ".join(problems))
    with run.step("cameras kept recording during the hammering"):
        rig.need_recording()
        state = rig.health()
        run.check(not state["not_receiving"] and not state["failed"], f"{state}")


def _disk_full(rig: _Rig) -> None:
    run, ctx = rig.run, rig.ctx
    outcomes = []
    with run.step("add cameras until the disk is full"):
        rig.need_recorder()
        rig.load_started = rig.ctx.sampler.elapsed()
        for cam in rig.cams:
            spec = cam.spec
            try:
                rig.ids[spec.name] = rig.api.add_camera(spec.name, spec.username, spec.password,
                                                        retention_days=0.05)["camera_id"]
                outcomes.append((spec.name, None))
                ctx.reporter.note(f"{spec.name}: added")
            except ApiError as exc:
                outcomes.append((spec.name, exc))
                ctx.reporter.note(f"{spec.name}: refused with status {exc.status}: {str(exc.body)[:200]}")
        usage = shutil.disk_usage(rig.recorder.data_dir)
        ctx.reporter.note(f"data directory: {usage.used // 2**20} MB used of {usage.total // 2**20} MB")
        if all(exc is None for _, exc in outcomes):
            ctx.reporter.note("every camera fit on the disk")
    with run.step("the recorder refuses cleanly when the disk is full"):
        rig.need_recorder()
        refused = [(name, exc) for name, exc in outcomes if exc is not None]
        if not refused:
            run.skip("no camera was refused")
        unclear = [f"{name}: status {exc.status}" for name, exc in refused if not 400 <= exc.status <= 599]
        run.check(not unclear, f"refusals without an error status: {unclear}")
        run.check(rig.recorder.proc.poll() is None, "the recorder exited")
    with run.step("cameras that fit keep recording"):
        rig.need_recording()
        missing, waited = rig.wait_receiving(rig.names, 60)
        run.check(not missing, f"not receiving after {waited:.0f} s: {missing}")


def _iso(moment) -> str:
    return moment.strftime("%Y-%m-%dT%H:%M:%S.000Z")
