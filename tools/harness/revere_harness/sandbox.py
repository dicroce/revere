"""All test work runs in a new user, network, mount and PID namespace.
The harness never signals a process by name.
guard() aborts the run if a recorder or viewer from outside is visible.
isolate() hides session sockets and the journal to prevent leakage."""

import os
import sys
import shutil
import subprocess
import psutil
import pathlib
import socket
import threading

ENV_FLAG = "REVERE_HARNESS_IN_SANDBOX"

HIDDEN_DIRS = ["/run/dbus", "/run/user", "/tmp/.X11-unix", "/run/systemd/journal"]
SCRUBBED_ENV = ["DBUS_SESSION_BUS_ADDRESS", "DBUS_SYSTEM_BUS_ADDRESS", "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR", "XDG_SESSION_ID", "HYPRLAND_INSTANCE_SIGNATURE", "SWAYSOCK", "PULSE_SERVER", "PIPEWIRE_RUNTIME_DIR", "SSH_AUTH_SOCK"]


class SandboxError(RuntimeError):
    pass


class SyslogSink:
    """Receives the syslog lines of processes in the sandbox and appends them to a file."""

    def __init__(self, socket_path: str, log_path: pathlib.Path):
        self.socket_path = socket_path
        self.log_path = log_path
        self.started = False
        self._thread = None
        self._socket = None

    def start(self) -> None:
        if self.started:
            return
        try:
            os.unlink(self.socket_path)
        except FileNotFoundError:
            pass

        sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        sock.settimeout(1.0)
        sock.bind(self.socket_path)
        os.chmod(self.socket_path, 0o666)

        self._socket = sock
        # Set before the thread starts: its loop runs while this is true.
        self.started = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        if not self.started:
            return
        self.started = False
        if self._socket:
            try:
                self._socket.close()
            except OSError:
                pass
            self._socket = None
        if self._thread:
            self._thread.join(timeout=2.0)
            self._thread = None

    def _run(self):
        while self.started:
            try:
                data, addr = self._socket.recvfrom(4096)
                line = data.decode("utf-8", errors="replace").strip()
                if line:
                    with open(self.log_path, "a") as f:
                        f.write(line + "\n")
                        f.flush()
            except socket.timeout:
                continue
            except OSError:
                break


def in_sandbox() -> bool:
    """Check if the current process is running inside the sandbox."""
    return os.environ.get(ENV_FLAG) == "1"


def host_recorders() -> list[dict]:
    """Find visible recorder or viewer processes on the host."""
    result = []
    for proc in psutil.process_iter(["pid", "name", "create_time"]):
        try:
            if proc.info["name"] in ("revere", "vision"):
                result.append({
                    "pid": proc.pid,
                    "create_time": proc.info["create_time"],
                    "name": proc.info["name"],
                })
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    return result


def still_alive(procs: list[dict]) -> list[dict]:
    """Filter out processes that are no longer alive."""
    gone = []
    for entry in procs:
        try:
            p = psutil.Process(entry["pid"])
            if abs(p.create_time() - entry["create_time"]) >= 0.01:
                gone.append(entry)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            gone.append(entry)
    return gone


def run_in_sandbox(argv: list[str]) -> int:
    """Run a command inside an unshare sandbox."""
    unshare = shutil.which("unshare")
    if not unshare:
        raise SandboxError("unshare program not found")

    env = os.environ.copy()
    env[ENV_FLAG] = "1"

    cmd = [
        unshare,
        "--user", "--map-root-user",
        "--net", "--mount", "--pid",
        "--fork", "--mount-proc", "--kill-child",
    ] + argv

    proc = subprocess.Popen(
        cmd,
        env=env,
        stdin=sys.stdin,
        stdout=sys.stdout,
        stderr=sys.stderr,
    )
    try:
        return proc.wait()
    except KeyboardInterrupt:
        return proc.wait()


def guard(check_network: bool = True) -> None:
    """Abort if the environment is not a clean sandbox."""
    if not in_sandbox():
        raise SandboxError("not running inside sandbox")

    pid = os.getpid()
    ppid = os.getppid()
    if pid != 1 or ppid != 0:
        raise SandboxError(f"PID is {pid}, PPID is {ppid}; expected PID 1, PPID 0")

    try:
        with open("/proc/1/cmdline", "rb") as f:
            cmdline = f.read().decode("utf-8", errors="replace")
        if "revere_harness" not in cmdline:
            raise SandboxError("PID 1 cmdline does not contain revere_harness")
    except OSError:
        raise SandboxError("cannot read /proc/1/cmdline")

    recorders = host_recorders()
    if recorders:
        raise SandboxError(f"host recorder or viewer visible: {recorders}")

    if check_network:
        interfaces = visible_interfaces()
        for iface in interfaces:
            if iface not in ("lo", "br0") and not iface.startswith("vc"):
                raise SandboxError(f"unexpected network interface visible: {iface}")


def isolate(log_path: pathlib.Path) -> "SyslogSink":
    """Mount sysfs, hide dirs, scrub env, check loopback, start syslog sink."""
    # Mount private sysfs so /sys/class/net shows this namespace's interfaces
    try:
        subprocess.run(["mount", "-t", "sysfs", "sysfs", "/sys"], check=True)
    except subprocess.CalledProcessError as e:
        raise SandboxError(f"failed to mount sysfs: {e}")

    # Hide desktop session sockets and system journal
    for d in HIDDEN_DIRS:
        if os.path.exists(d):
            try:
                subprocess.run(["mount", "-t", "tmpfs", "tmpfs", d], check=True)
            except subprocess.CalledProcessError as e:
                raise SandboxError(f"failed to mount tmpfs on {d}: {e}")

    # Remove sensitive environment variables
    for key in SCRUBBED_ENV:
        os.environ.pop(key, None)

    # Ensure only loopback is visible
    check_only_loopback()

    # Start syslog sink if journal directory exists
    if os.path.exists("/run/systemd/journal"):
        sink = SyslogSink("/run/systemd/journal/dev-log", log_path)
        sink.start()
        return sink
    return SyslogSink("/run/systemd/journal/dev-log", log_path)


def check_only_loopback() -> None:
    """Raise SandboxError if any interface other than lo is visible."""
    interfaces = visible_interfaces()
    if interfaces != ["lo"]:
        raise SandboxError(f"expected only loopback, found: {interfaces}")


def visible_interfaces() -> list[str]:
    """Return sorted list of network interface names."""
    return sorted(os.listdir("/sys/class/net"))


def bring_up_loopback() -> None:
    """Bring up the loopback interface inside the sandbox."""
    try:
        subprocess.run(
            ["ip", "link", "set", "lo", "up"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError as e:
        raise SandboxError(f"failed to bring up loopback: {e}")