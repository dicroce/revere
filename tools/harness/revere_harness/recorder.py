import json
import os
import pathlib
import signal
import subprocess
import time
import urllib.error
import urllib.request


class RecorderError(RuntimeError): ...


class Recorder:
    """Starts and stops one headless recorder process with its own temporary directories."""

    API = "http://127.0.0.1:8088"

    def __init__(self, revere_dir: pathlib.Path, work_dir: pathlib.Path) -> None:
        self.revere_dir = revere_dir.resolve()
        self.work_dir = work_dir.resolve()
        self.data_dir = self.work_dir / "data"
        self.secret_dir = self.work_dir / "secret"
        self.home_dir = self.work_dir / "home"
        self.log_path = self.work_dir / "recorder.log"
        self.proc: subprocess.Popen | None = None
        self._log_file = None

    def start(self, ready_timeout: float = 30.0) -> None:
        binary = self.revere_dir / "revere"
        if not binary.is_file():
            raise RecorderError(f"revere binary not found at {binary}")

        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.secret_dir.mkdir(parents=True, exist_ok=True)
        self.home_dir.mkdir(parents=True, exist_ok=True)

        env = os.environ.copy()
        env["HOME"] = str(self.home_dir)
        env["XDG_DATA_HOME"] = str(self.home_dir / ".local" / "share")
        env["XDG_CONFIG_HOME"] = str(self.home_dir / ".config")
        env["XDG_CACHE_HOME"] = str(self.home_dir / ".cache")

        ld_lib_path = self.revere_dir.as_posix()
        if "LD_LIBRARY_PATH" in env:
            ld_lib_path = ld_lib_path + ":" + env["LD_LIBRARY_PATH"]
        env["LD_LIBRARY_PATH"] = ld_lib_path

        cmd = [
            str(binary),
            "--headless",
            "--data-dir", str(self.data_dir),
            "--secret-dir", str(self.secret_dir),
        ]

        try:
            self._log_file = self.log_path.open("w")
            self.proc = subprocess.Popen(
                cmd,
                cwd=str(self.revere_dir),
                env=env,
                stdout=self._log_file,
                stderr=subprocess.STDOUT,
            )
        except OSError as e:
            raise RecorderError(f"Failed to start recorder: {e}")

        if self.proc.poll() is not None:
            log_lines = self._read_last_log(20)
            raise RecorderError(f"Recorder exited immediately with code {self.proc.returncode}. Log:\n{log_lines}")

        deadline = time.monotonic() + ready_timeout
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                log_lines = self._read_last_log(20)
                raise RecorderError(f"Recorder exited during startup with code {self.proc.returncode}. Log:\n{log_lines}")
            try:
                status, _ = self.get("/version")
                if status == 200:
                    return
            except RecorderError:
                pass
            time.sleep(0.25)

        self.stop(timeout=5.0)
        log_lines = self._read_last_log(20)
        raise RecorderError(f"Recorder did not become ready within {ready_timeout}s. Log:\n{log_lines}")

    def get(self, path: str, token: str | None = None, timeout: float = 5.0) -> tuple[int, bytes]:
        if self.proc is None or self.proc.poll() is not None:
            raise RecorderError("Recorder is not running")
        
        url = self.API + path
        headers = {}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        
        req = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return (resp.status, resp.read())
        except urllib.error.HTTPError as e:
            return (e.code, e.read())
        except urllib.error.URLError as e:
            raise RecorderError(f"Network error: {e.reason}")

    def get_json(self, path: str, token: str | None = None) -> tuple[int, object]:
        status, body = self.get(path, token)
        try:
            data = json.loads(body.decode("utf-8"))
        except json.JSONDecodeError:
            data = body
        return (status, data)

    def stop(self, timeout: float = 20.0) -> int:
        if self.proc is None:
            return 0
        
        if self.proc.poll() is not None:
            return self.proc.returncode

        try:
            # only the process this object started is ever signalled
            self.proc.send_signal(signal.SIGINT)
        except ProcessLookupError:
            return self.proc.returncode

        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                self.proc.kill()
            except ProcessLookupError:
                pass
            self.proc.wait(timeout=5.0)
        
        if self._log_file is not None:
            self._log_file.close()
            self._log_file = None

        return self.proc.returncode

    @property
    def pid(self) -> int | None:
        if self.proc is not None:
            return self.proc.pid
        return None

    def _read_last_log(self, lines: int) -> str:
        if not self.log_path.exists():
            return ""
        with self.log_path.open("r") as f:
            content = f.read()
        if not content:
            return ""
        return "\n".join(content.splitlines()[-lines:])