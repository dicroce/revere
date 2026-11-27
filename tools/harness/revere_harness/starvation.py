"""Whether the fake cameras kept up with real time during a run.

A fake camera encodes its own video. When the host has no CPU to spare it delivers video slower than real time, and the
recorder then reconnects to it to drop the backlog ("ingest lag ... reconnecting to drop the backlog"): the recording
has a gap that is the host's doing, not the recorder's. The recorder says how fast each camera's clock ran in its log,
and the sampler says how much CPU each fake camera used; this reads both so the run says "the host could not keep
up" as a failure of its own, in plain words, instead of leaving the gaps to be read as recorder faults.
"""
from __future__ import annotations

import csv
import pathlib
import re

from revere_harness.steps import Run

# A camera's clock running slower than this share of real time is not keeping up.
MIN_CAMERA_TIME = 0.9
# A fake camera using more than this much CPU on average (100 is one core) is starving.
PINNED_CPU = 90.0
# Suites that put fake cameras under a running recorder.
SUITES = ("recording", "stress")

_LAG = re.compile(r"ingest lag\[(?P<cam>[^\]]+)\]:.*\(camera time at (?P<x>[0-9.]+)x real time\)(?P<reconnect>; reconnecting to drop the backlog)?")


def slow_cameras(syslog: str, minimum: float = MIN_CAMERA_TIME) -> tuple[dict[str, float], dict[str, float]]:
    """(the cameras the recorder reconnected to for lag, the cameras whose clock only dipped under `minimum`).

    Each with its slowest reading. A dip that the recorder rode out (no reconnect) leaves no gap, so it is a note, not
    a failure; a reconnect is a gap the host caused.
    """
    reconnected: dict[str, float] = {}
    dipped: dict[str, float] = {}
    for line in syslog.splitlines():
        m = _LAG.search(line)
        if not m:
            continue
        x = float(m.group("x"))
        cam = m.group("cam")
        if m.group("reconnect"):
            reconnected[cam] = min(x, reconnected.get(cam, x))
        elif x < minimum:
            dipped[cam] = min(x, dipped.get(cam, x))
    return reconnected, {c: x for c, x in dipped.items() if c not in reconnected}


def pinned_cameras(resources_csv: str, limit: float = PINNED_CPU) -> dict[str, float]:
    """Each fake camera (label cam*) whose CPU averaged over `limit`, while it was doing anything, with the average."""
    totals: dict[str, list[float]] = {}
    for row in csv.DictReader(resources_csv.splitlines()):
        label = row.get("label", "")
        if not label.startswith("cam"):
            continue
        try:
            cpu = float(row["cpu_percent"])
        except (KeyError, ValueError):
            continue
        if cpu > 1.0:
            totals.setdefault(label, []).append(cpu)
    return {k: sum(v) / len(v) for k, v in totals.items() if v and sum(v) / len(v) > limit}


def words(slow: dict[str, float], pinned: dict[str, float]) -> str:
    parts = []
    if slow:
        parts.append("the recorder reconnected to drop a backlog on " + ", ".join(f"{c} (at {x:.2f}x real time)" for c, x in sorted(slow.items())))
    if pinned:
        parts.append("fake cameras used a core or more on average: " + ", ".join(f"{c} {p:.0f}%" for c, p in sorted(pinned.items())))
    return ("; ".join(parts) + ". The host could not keep up with the fake cameras, so gaps and reconnects in this run "
            "are likely the host's, not the recorder's: lower the pictures' size or rate, or give the host more CPUs.")


def check(run: Run, out_dir: pathlib.Path) -> None:
    """Add one step to the run: the fake cameras kept up. Only for the suites that record from them."""
    if run.suite not in SUITES:
        return
    log = out_dir / "syslog.log"
    csv_path = out_dir / "resources.csv"
    slow, dipped = slow_cameras(log.read_text(errors="replace")) if log.exists() else ({}, {})
    pinned = pinned_cameras(csv_path.read_text(errors="replace")) if csv_path.exists() else {}
    if dipped:
        run.reporter.note("a fake camera's clock dipped under real time, and the recorder rode it out: "
                          + ", ".join(f"{c} {x:.2f}x" for c, x in sorted(dipped.items())))
    with run.step("the fake cameras kept up with real time"):
        if slow or pinned:
            raise AssertionError(words(slow, pinned))
