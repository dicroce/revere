from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from queue import Queue, Empty
from typing import Any

from revere_harness import net


@dataclass
class CameraSpec:
    name: str
    index: int
    video: str = "h264"
    audio: str = "none"
    substream: bool = False
    username: str = "admin"
    password: str = "harness-pw"
    width: int = 1280
    height: int = 720
    fps: int = 15
    bitrate_kbps: int = 1500
    scene: str = "moving"

    @property
    def ip(self) -> str:
        return f"10.77.0.{10 + self.index}"

    @property
    def mac(self) -> str:
        high = self.index // 256
        low = self.index % 256
        return f"02:00:00:00:{high:02x}:{low:02x}"

    @property
    def serial(self) -> str:
        return f"FAKE{self.index:04d}"


class CameraError(RuntimeError):
    pass


def _get_log_tail(log_path: pathlib.Path) -> str:
    """Read the last 20 lines of the log file."""
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        return "".join(lines[-20:])
    except FileNotFoundError:
        return ""


class FakeCamera:
    def __init__(self, spec: CameraSpec, log_dir: pathlib.Path) -> None:
        self.spec = spec
        self.ip = spec.ip
        self.mac = spec.mac
        self.proc: subprocess.Popen[str] | None = None
        self._log_path = log_dir / f"{spec.name}.log"
        log_dir.mkdir(parents=True, exist_ok=True)

    def _start_process(self) -> None:
        config = {
            "name": self.spec.name,
            "serial": self.spec.serial,
            "manufacturer": "Harness",
            "model": "FakeCam",
            "username": self.spec.username,
            "password": self.spec.password,
            "rtsp_port": 554,
            "onvif_port": 80,
            "video": self.spec.video,
            "audio": self.spec.audio,
            "width": self.spec.width,
            "height": self.spec.height,
            "fps": self.spec.fps,
            "bitrate_kbps": self.spec.bitrate_kbps,
            "substream": self.spec.substream,
            "scene": self.spec.scene,
        }

        cmd = [
            sys.executable,
            "-m",
            "revere_harness.fakecam",
            json.dumps(config),
        ]

        log_fh = open(self._log_path, "w", encoding="utf-8")
        try:
            self.proc = subprocess.Popen(
                ["unshare", "--net", "--"] + cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=log_fh,
                text=True,
                bufsize=1,
            )
        except Exception as exc:
            log_fh.close()
            raise CameraError(f"Failed to start camera process: {exc}") from exc

    def _start_reader(self) -> Queue[str]:
        queue: Queue[str] = Queue()

        def reader_thread() -> None:
            try:
                for line in self.proc.stdout:  # type: ignore[union-attr]
                    queue.put(line)
            except Exception:
                pass

        t = threading.Thread(target=reader_thread, daemon=True)
        t.start()
        return queue

    def _wait_for_event(self, event_key: str, event_value: Any, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self._stdout_queue.get(timeout=min(0.1, deadline - time.monotonic()))
            except Empty:
                continue
            
            if self.proc.poll() is not None:
                tail = _get_log_tail(self._log_path)
                raise CameraError(f"Process exited with code {self.proc.returncode}. Log tail:\n{tail}")

            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            
            if obj.get(event_key) == event_value:
                return

        tail = _get_log_tail(self._log_path)
        self.stop()
        raise CameraError(f"Camera did not become ready. Log tail:\n{tail}")

    def start(self, timeout: float = 20.0) -> None:
        self._start_process()
        self._stdout_queue = self._start_reader()

        try:
            self._wait_for_event("event", "ready", timeout)
        except CameraError:
            raise
        
        # Check if process died during wait (poll might return None if we just started, 
        # but _wait_for_event checks poll inside loop. If it returns here, it didn't die yet).
        # However, if the queue was empty and timeout hit, we raised above.
        # If we are here, we found "ready".

        try:
            net.attach(self.spec.index, self.proc.pid, self.ip, self.mac)
        except Exception as exc:
            self.stop()
            raise CameraError(f"Failed to attach network: {exc}") from exc

        try:
            self.command("start")
        except CameraError as exc:
            self.stop()
            raise exc

    def command(self, cmd: str, **fields: Any) -> dict[str, Any]:
        if not self.proc or not self.proc.stdin:
            raise CameraError("Camera process is not running")

        payload = {"cmd": cmd}
        payload.update(fields)
        line = json.dumps(payload) + "\n"

        try:
            self.proc.stdin.write(line)
            self.proc.stdin.flush()
        except BrokenPipeError:
            raise CameraError("Camera process closed stdin unexpectedly") from None

        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                tail = _get_log_tail(self._log_path)
                raise CameraError(f"Process exited with code {self.proc.returncode} during command. Log tail:\n{tail}")

            try:
                q_line = self._stdout_queue.get(timeout=min(0.1, deadline - time.monotonic()))
            except Empty:
                continue
            
            q_line = q_line.strip()
            if not q_line:
                continue
            try:
                obj = json.loads(q_line)
            except json.JSONDecodeError:
                continue
            
            if obj.get("cmd") == cmd:
                if not obj.get("ok"):
                    raise CameraError(f"Command {cmd} failed: {obj.get('error')}")
                return obj

        tail = _get_log_tail(self._log_path)
        raise CameraError(f"Timeout waiting for response to command {cmd}. Log tail:\n{tail}")

    def rtsp_url(self, stream: str = "main", with_auth: bool = True) -> str:
        user_pass = ""
        if self.spec.username and with_auth:
            user_pass = f"{self.spec.username}:{self.spec.password}@"
        return f"rtsp://{user_pass}{self.ip}:554/{stream}"

    def set_ip(self, ip: str) -> None:
        net.set_ip(self.proc.pid, ip)  # type: ignore[arg-type]
        net.flush_neighbours()
        self.ip = ip

    def set_mac(self, mac: str) -> None:
        net.set_mac(self.proc.pid, mac)  # type: ignore[arg-type]
        net.flush_neighbours()
        self.mac = mac

    def unplug(self) -> None:
        net.set_link(self.spec.index, False)

    def plug(self) -> None:
        net.set_link(self.spec.index, True)

    def slow(self, delay_ms: int = 0, loss_percent: float = 0.0, rate_kbit: int = 0) -> None:
        net.set_slow(self.spec.index, delay_ms, loss_percent, rate_kbit)

    def normal(self) -> None:
        net.set_slow(self.spec.index)

    def rtsp(self, on: bool) -> None:
        self.command("rtsp", on=on)

    def onvif(self, on: bool) -> None:
        self.command("onvif", on=on)

    def stall(self, on: bool) -> None:
        self.command("stall", on=on)

    def scene(self, name: str) -> None:
        """Switch the picture: "moving" (motion in the scene) or "static" (no motion)."""
        self.command("scene", name=name)

    def requests_seen(self) -> list[str]:
        resp = self.command("seen")
        return resp.get("requests", [])

    def stop(self) -> None:
        if not self.proc or self.proc.poll() is not None:
            return

        try:
            self.command("quit")
        except CameraError:
            pass

        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            ret = self.proc.poll()
            if ret is not None:
                break
            time.sleep(0.1)

        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()

    @property
    def pid(self) -> int | None:
        return self.proc.pid if self.proc else None