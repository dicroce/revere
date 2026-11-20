"""All test work runs in a new user, network, mount and PID namespace.
The harness never signals a process by name.
guard() aborts the run if a recorder or viewer from outside is visible."""

import os
import sys
import shutil
import subprocess
import psutil

ENV_FLAG = "REVERE_HARNESS_IN_SANDBOX"


class SandboxError(RuntimeError):
    pass


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


def guard() -> None:
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