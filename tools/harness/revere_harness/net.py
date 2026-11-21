"""Test network plumbing: a bridge, and one veth pair per fake camera namespace."""
from __future__ import annotations

import subprocess

BRIDGE = "br0"
BRIDGE_IP = "10.77.0.1"
PREFIX = 24


class NetError(RuntimeError):
    pass


created: set[str] = set()


def created_interfaces() -> set[str]:
    return set(created)


def sh(args: list[str], netns_pid: int | None = None) -> str:
    if netns_pid is not None:
        args = ["nsenter", f"--net=/proc/{netns_pid}/ns/net"] + args
    try:
        result = subprocess.run(
            args,
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout
    except subprocess.CalledProcessError as exc:
        raise NetError(f"Command {' '.join(args)} failed: {exc.stderr}") from exc


def setup_bridge() -> None:
    try:
        sh(["ip", "link", "show", BRIDGE])
    except NetError:
        pass
    else:
        created.add(BRIDGE)
        return

    sh(["ip", "link", "add", BRIDGE, "type", "bridge", "mcast_snooping", "0"])
    sh(["ip", "addr", "add", f"{BRIDGE_IP}/{PREFIX}", "dev", BRIDGE])
    sh(["ip", "link", "set", BRIDGE, "up"])
    sh(["ip", "route", "add", "239.255.255.250/32", "dev", BRIDGE])
    created.add(BRIDGE)


def attach(index: int, pid: int, ip: str, mac: str) -> str:
    veth_name = f"vc{index}"
    sh(["ip", "link", "add", veth_name, "type", "veth", "peer", "name", "eth0", "netns", str(pid)])
    sh(["ip", "link", "set", veth_name, "master", BRIDGE])
    sh(["ip", "link", "set", veth_name, "up"])

    sh(["ip", "link", "set", "lo", "up"], netns_pid=pid)
    sh(["ip", "link", "set", "eth0", "address", mac], netns_pid=pid)
    sh(["ip", "addr", "add", f"{ip}/{PREFIX}", "dev", "eth0"], netns_pid=pid)
    sh(["ip", "link", "set", "eth0", "up"], netns_pid=pid)
    sh(["ip", "route", "add", "239.0.0.0/8", "dev", "eth0"], netns_pid=pid)

    created.add(veth_name)
    return veth_name


def set_ip(pid: int, ip: str) -> None:
    sh(["ip", "addr", "flush", "dev", "eth0"], netns_pid=pid)
    sh(["ip", "addr", "add", f"{ip}/{PREFIX}", "dev", "eth0"], netns_pid=pid)
    sh(["ip", "route", "replace", "239.0.0.0/8", "dev", "eth0"], netns_pid=pid)


def set_mac(pid: int, mac: str) -> None:
    sh(["ip", "link", "set", "eth0", "down"], netns_pid=pid)
    sh(["ip", "link", "set", "eth0", "address", mac], netns_pid=pid)
    sh(["ip", "link", "set", "eth0", "up"], netns_pid=pid)
    sh(["ip", "route", "replace", "239.0.0.0/8", "dev", "eth0"], netns_pid=pid)


def set_link(index: int, up: bool) -> None:
    veth_name = f"vc{index}"
    state = "up" if up else "down"
    sh(["ip", "link", "set", veth_name, state])


def set_slow(index: int, delay_ms: int = 0, loss_percent: float = 0.0, rate_kbit: int = 0) -> None:
    veth_name = f"vc{index}"
    try:
        sh(["tc", "qdisc", "del", "dev", veth_name, "root"])
    except NetError:
        pass

    if delay_ms == 0 and loss_percent == 0.0 and rate_kbit == 0:
        return

    parts = ["tc", "qdisc", "add", "dev", veth_name, "root", "netem"]
    if delay_ms > 0:
        parts.extend(["delay", f"{delay_ms}ms"])
    if loss_percent > 0.0:
        parts.extend(["loss", f"{loss_percent}%"])
    if rate_kbit > 0:
        parts.extend(["rate", f"{rate_kbit}kbit"])

    sh(parts)


def flush_neighbours() -> None:
    sh(["ip", "neigh", "flush", "dev", BRIDGE])


def arp_mac(ip: str) -> str | None:
    try:
        output = sh(["cat", "/proc/net/arp"])
    except NetError:
        return None

    for line in output.strip().splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        if parts[0] != ip:
            continue
        mac = parts[3].lower()
        if mac != "00:00:00:00:00:00":
            return mac
    return None