"""Smoke suite: the sandbox hides outside processes, and a headless recorder starts, answers, and stops cleanly."""
from __future__ import annotations

import time
import psutil

from revere_harness.sandbox import guard
from revere_harness.recorder import Recorder
from revere_harness.steps import Run


def run(run: Run, ctx: object) -> None:
    recorder = None
    started = False
    try:
        with run.step("sandbox hides host processes"):
            guard()

        with run.step("recorder starts headless"):
            work_dir = ctx.out_dir / "recorder"
            recorder = Recorder(ctx.revere_dir, work_dir)
            recorder.start()
            started = True
            pid = recorder.pid
            if pid is None:
                raise RuntimeError("Recorder process PID is None after start")
            ctx.sampler.watch("recorder", pid)

        with run.step("GET /version returns build details"):
            if not started or recorder is None or recorder.proc is None:
                run.skip("recorder did not start")
            status, body = recorder.get_json("/version")
            run.check(status == 200, f"Expected status 200, got {status}")
            data = body
            run.check(isinstance(data, dict), f"Response is not a JSON object: {type(data)}")
            run.check("version" in data and isinstance(data["version"], str) and len(data["version"]) > 0,
                      f"Missing or invalid version in response: {str(data)[:200]}")
            run.check("recorder_id" in data, f"Missing recorder_id in response: {str(data)[:200]}")

        with run.step("GET /auth_status is open"):
            if not started or recorder is None or recorder.proc is None:
                run.skip("recorder did not start")
            status, body = recorder.get_json("/auth_status")
            run.check(status == 200, f"Expected status 200, got {status}")
            data = body
            run.check(isinstance(data, dict), f"Response is not a JSON object: {type(data)}")
            run.check("password_set" in data, f"Missing password_set in response: {str(data)[:200]}")

        with run.step("GET /cameras without a token is refused"):
            if not started or recorder is None or recorder.proc is None:
                run.skip("recorder did not start")
            status, _ = recorder.get("/cameras")
            run.check(status == 401, f"Expected status 401, got {status}")

        with run.step("recorder keeps running for 3 seconds"):
            if not started or recorder is None or recorder.proc is None:
                run.skip("recorder did not start")
            time.sleep(3)
            poll = recorder.proc.poll()
            run.check(poll is None, f"Recorder exited prematurely with code {poll}")

        with run.step("recorder stops cleanly on SIGINT"):
            if not started or recorder is None or recorder.proc is None:
                run.skip("recorder did not start")
            exit_code = recorder.stop()
            run.check(exit_code == 0, f"Expected exit code 0, got {exit_code}")

        with run.step("no process left behind"):
            children = psutil.Process().children(recursive=True)
            revere_procs = [p for p in children if p.name() == "revere"]
            run.check(len(revere_procs) == 0, f"Found {len(revere_procs)} leftover 'revere' processes")

    finally:
        if recorder is not None:
            try:
                recorder.stop()
            except Exception:
                pass