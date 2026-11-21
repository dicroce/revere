"""Local web page to start suites, watch them run, and look at past runs.

The page can start processes on this machine, so the server listens on
loopback only, needs a secret token on every request, and checks the Host
header. It starts at most one run at a time and only ever signals the run it
started, through its process handle.
"""
from __future__ import annotations

import datetime
import hmac
import json
import os
import pathlib
import re
import secrets
import signal
import subprocess
import sys
import threading
import time
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

from revere_harness import catalog

RUN_ID = re.compile(r"^[0-9]{8}-[0-9]{6}-[a-z_]+$")
MAX_BODY = 64 * 1024
CONTENT_TYPES = {
    ".jpg": "image/jpeg",
    ".png": "image/png",
    ".json": "application/json",
    ".jsonl": "text/plain; charset=utf-8",
    ".md": "text/plain; charset=utf-8",
    ".log": "text/plain; charset=utf-8",
    ".csv": "text/plain; charset=utf-8",
    ".xml": "text/plain; charset=utf-8",
}


class _State:
    """What the server knows about the one run it may have started."""

    def __init__(self, harness_dir: pathlib.Path, runs_dir: pathlib.Path, port: int,
                 revere_dir: pathlib.Path | None = None):
        self.revere_dir = revere_dir
        self.harness_dir = harness_dir
        self.runs_dir = runs_dir
        self.hosts = {f"127.0.0.1:{port}", f"localhost:{port}"}
        self.token = secrets.token_urlsafe(24)
        self.lock = threading.Lock()
        self.proc: subprocess.Popen | None = None
        self.run: dict | None = None

    def running(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def status(self) -> dict:
        with self.lock:
            return {"running": self.running(), "run": self.run if self.running() else None}

    def start(self, suite: str, params: dict, keep: bool) -> str:
        with self.lock:
            if self.running():
                raise RuntimeError("a run is already in progress")
            run_id = f"{datetime.datetime.now():%Y%m%d-%H%M%S}-{suite}"
            out_dir = self.runs_dir / run_id
            out_dir.mkdir(parents=True, exist_ok=False)
            command = [sys.executable, "-m", "revere_harness", suite, "--out", str(out_dir)]
            if self.revere_dir:
                command += ["--revere-dir", str(self.revere_dir)]
            if keep:
                command.append("--keep")
            for name, value in params.items():
                command += ["--param", f"{name}={value}"]
            env = dict(os.environ)
            env["PYTHONPATH"] = str(self.harness_dir) + os.pathsep + env.get("PYTHONPATH", "")
            with open(out_dir / "console.log", "wb") as console:
                # Its own session, so a Ctrl-C in the server's terminal doesn't also hit the run.
                self.proc = subprocess.Popen(command, cwd=self.harness_dir, env=env, stdin=subprocess.DEVNULL,
                                             stdout=console, stderr=subprocess.STDOUT, start_new_session=True)
            self.run = {"id": run_id, "suite": suite, "started": time.time()}
            return run_id

    def stop(self) -> bool:
        with self.lock:
            if not self.running():
                return False
            # SIGINT lets the harness stop its recorder and cameras and write its report.
            # The run is the leader of its own process group (start_new_session), so the
            # group holds exactly the processes this server started.
            os.killpg(self.proc.pid, signal.SIGINT)
            return True

    def runs(self) -> list[dict]:
        found = []
        if self.runs_dir.is_dir():
            for folder in sorted(self.runs_dir.iterdir(), reverse=True):
                if not folder.is_dir() or not RUN_ID.match(folder.name):
                    continue
                entry = {"id": folder.name, "suite": folder.name.split("-", 2)[2], "finished": False,
                         "passed": 0, "failed": 0, "skipped": 0, "seconds": 0}
                try:
                    summary = json.loads((folder / "results.json").read_text())["summary"]
                    entry.update(finished=True, passed=summary["passed"], failed=summary["failed"],
                                 skipped=summary["skipped"], seconds=summary["seconds"])
                except (OSError, ValueError, KeyError):
                    pass
                found.append(entry)
        return found[:200]


class _Handler(BaseHTTPRequestHandler):
    state: _State  # set on the bound subclass in serve()
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):  # noqa: A002 - the base class's signature
        pass

    # ---- gate: every request passes here before any routing ----

    def _gate(self) -> tuple[str, dict] | None:
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        if self.headers.get("Host", "") not in self.state.hosts:
            # A page on another site can make the browser send requests here under its own name.
            self._json(403, {"error": "forbidden host"})
            return None
        given = (query.get("token") or [""])[0]
        self._token_from_query = bool(given)
        if not given:
            cookie = SimpleCookie(self.headers.get("Cookie", ""))
            given = cookie["harness_token"].value if "harness_token" in cookie else ""
        if not given or not hmac.compare_digest(given.encode(), self.state.token.encode()):
            self._json(403, {"error": "missing or wrong token"})
            return None
        return url.path, query

    def do_GET(self) -> None:
        gate = self._gate()
        if gate is None:
            return
        path, query = gate
        state = self.state
        if path == "/":
            page = (state.harness_dir / "revere_harness" / "web_page.html").read_bytes()
            headers = {}
            if self._token_from_query:
                headers["Set-Cookie"] = f"harness_token={state.token}; HttpOnly; SameSite=Strict; Path=/"
            self._send(200, "text/html; charset=utf-8", page, headers)
        elif path == "/api/suites":
            self._json(200, {"suites": catalog.SUITES})
        elif path == "/api/status":
            self._json(200, state.status())
        elif path == "/api/runs":
            self._json(200, {"runs": state.runs()})
        else:
            parts = path.split("/")
            # /api/runs/<id>/events  or  /api/runs/<id>/file/<relative path>
            if len(parts) >= 5 and parts[1:3] == ["api", "runs"] and RUN_ID.match(parts[3]):
                folder = state.runs_dir / parts[3]
                if parts[4] == "events" and len(parts) == 5:
                    self._events(folder, query)
                    return
                if parts[4] == "file" and len(parts) > 5:
                    self._file(folder, "/".join(parts[5:]))
                    return
            self._json(404, {"error": "not found"})

    def do_POST(self) -> None:
        gate = self._gate()
        if gate is None:
            return
        path, _ = gate
        length = int(self.headers.get("Content-Length", 0) or 0)
        if length > MAX_BODY:
            self._json(413, {"error": "request too large"})
            return
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
            if not isinstance(body, dict):
                raise ValueError("the body must be a JSON object")
        except ValueError as exc:
            self._json(400, {"error": f"bad JSON: {exc}"})
            return

        if path == "/api/run":
            try:
                suite = str(body.get("suite", ""))
                params = catalog.validate(suite, body.get("params") or {})
            except (ValueError, TypeError) as exc:
                self._json(400, {"error": str(exc)})
                return
            try:
                run_id = self.state.start(suite, params, bool(body.get("keep")))
            except RuntimeError as exc:
                self._json(409, {"error": str(exc)})
                return
            self._json(200, {"id": run_id})
        elif path == "/api/stop":
            if self.state.stop():
                self._json(200, {"stopping": True})
            else:
                self._json(409, {"error": "nothing is running"})
        else:
            self._json(404, {"error": "not found"})

    # ---- replies ----

    def _send(self, status: int, content_type: str, data: bytes, headers: dict | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status: int, obj: object) -> None:
        self._send(status, "application/json", json.dumps(obj).encode())

    def _file(self, folder: pathlib.Path, relative: str) -> None:
        try:
            root = folder.resolve()
            target = (root / relative).resolve()
            # After resolving, the file must still be inside this run's folder.
            if root not in target.parents or not target.is_file():
                raise FileNotFoundError(relative)
            data = target.read_bytes()
        except OSError:
            self._json(404, {"error": "not found"})
            return
        self._send(200, CONTENT_TYPES.get(target.suffix.lower(), "application/octet-stream"), data)

    def _events(self, folder: pathlib.Path, query: dict) -> None:
        if not folder.is_dir():
            self._json(404, {"error": "not found"})
            return
        try:
            index = max(0, int((query.get("from") or ["0"])[0]))
        except ValueError:
            index = 0
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        path = folder / "events.jsonl"
        sent, last_write = 0, time.monotonic()
        try:
            while True:
                lines = path.read_text(errors="replace").splitlines() if path.exists() else []
                finished = False
                for line in lines[max(sent, index):]:
                    if not line.strip():
                        continue
                    self.wfile.write(f"data: {line}\n\n".encode())
                    finished = finished or '"type": "run_finished"' in line
                    last_write = time.monotonic()
                sent = max(sent, len(lines))
                self.wfile.flush()
                if finished:
                    return
                state = self.state
                if not (state.running() and state.run and state.run["id"] == folder.name):
                    # Not the live run: everything there is has been sent.
                    self.wfile.write(b"event: end\ndata: {}\n\n")
                    self.wfile.flush()
                    return
                if time.monotonic() - last_write > 15:
                    self.wfile.write(b": keep-alive\n\n")
                    self.wfile.flush()
                    last_write = time.monotonic()
                time.sleep(0.3)
        except (BrokenPipeError, ConnectionResetError):
            return


def serve(harness_dir: pathlib.Path, runs_dir: pathlib.Path, port: int = 8090,
          revere_dir: pathlib.Path | None = None) -> int:
    """Serve the control page on loopback until interrupted."""
    state = _State(harness_dir, runs_dir, port, revere_dir)
    handler = type("BoundHandler", (_Handler,), {"state": state})
    ThreadingHTTPServer.daemon_threads = True
    try:
        # Loopback only, on purpose: never another address.
        server = ThreadingHTTPServer(("127.0.0.1", port), handler)
    except OSError as exc:
        print(f"error: cannot listen on 127.0.0.1:{port}: {exc}", file=sys.stderr)
        return 1
    print(f"http://127.0.0.1:{port}/?token={state.token}", flush=True)
    print("local only: this page can start test runs on this machine", flush=True)
    # A terminate signal should clean up the same way Ctrl-C does.
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if state.stop():
            print("stopping the run in progress...", flush=True)
            try:
                state.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                state.proc.kill()
        server.server_close()
    return 0
