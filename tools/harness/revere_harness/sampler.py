from __future__ import annotations

import csv
import pathlib
import threading
import time
from typing import Callable

import psutil


class Sampler:
    """Samples resource use of registered processes in a background thread."""

    def __init__(
        self,
        csv_path: pathlib.Path,
        interval: float = 1.0,
        on_sample: Callable[[dict], None] | None = None,
    ) -> None:
        self._csv_path = csv_path
        self._interval = interval
        self._on_sample = on_sample
        self._watched: dict[str, int] = {}  # label -> pid
        self._lock = threading.Lock()
        self._data: dict[str, list[dict]] = {}
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._start_time: float = 0.0
        self._procs: dict[str, psutil.Process] = {}

    def watch(self, label: str, pid: int) -> None:
        with self._lock:
            self._watched[label] = pid
            if label not in self._data:
                self._data[label] = []

    def unwatch(self, label: str) -> None:
        with self._lock:
            self._watched.pop(label, None)

    def rows(self, label: str) -> list[dict]:
        with self._lock:
            return list(self._data.get(label, []))

    def elapsed(self) -> float:
        """Seconds since sampling began: the clock the rows' "time" is on."""
        return time.time() - self._start_time

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        self._start_time = time.time()
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=5.0)

    def summary(self) -> dict[str, dict]:
        result: dict[str, dict] = {}
        with self._lock:
            for label, rows in self._data.items():
                if not rows:
                    continue
                cpu_max = 0.0
                rss_mb_max = 0.0
                threads_max = 0
                fds_max = 0
                for row in rows:
                    cpu = row.get("cpu_percent", 0.0) or 0.0
                    rss = row.get("rss_mb", 0.0) or 0.0
                    threads = row.get("threads", 0) or 0
                    fds = row.get("fds", 0) or 0

                    if cpu > cpu_max:
                        cpu_max = cpu
                    if rss > rss_mb_max:
                        rss_mb_max = rss
                    if threads > threads_max:
                        threads_max = threads
                    if fds > fds_max:
                        fds_max = fds

                result[label] = {
                    "samples": len(rows),
                    "cpu_percent_max": round(cpu_max, 1),
                    "rss_mb_max": round(rss_mb_max, 1),
                    "rss_mb_first": round(rows[0]["rss_mb"], 1),
                    "rss_mb_last": round(rows[-1]["rss_mb"], 1),
                    "threads_max": threads_max,
                    "fds_max": fds_max,
                }
        return result

    def _loop(self) -> None:
        while not self._stop_event.is_set():
            now = time.time()
            elapsed = now - self._start_time
            rows_to_write: list[dict] = []

            with self._lock:
                labels_to_check = list(self._watched.keys())
                for label in labels_to_check:
                    pid = self._watched[label]
                    try:
                        # CPU use is measured between two calls on the same object, so the object is kept:
                        # a new one each time would always read zero.
                        proc = self._procs.get(label)
                        if proc is None or proc.pid != pid:
                            proc = self._procs[label] = psutil.Process(pid)
                            proc.cpu_percent(interval=None)
                        cpu = proc.cpu_percent(interval=None)
                        mem = proc.memory_info()
                        rss_mb = mem.rss / (1024 * 1024)
                        vms_mb = mem.vms / (1024 * 1024)
                        threads = proc.num_threads()
                        fds = proc.num_fds()

                        row = {
                            "time": round(elapsed, 2),
                            "label": label,
                            "pid": pid,
                            "cpu_percent": round(cpu, 1),
                            "rss_mb": round(rss_mb, 1),
                            # Private memory: resident minus file-backed pages. The recorder maps its
                            # pre-allocated recording files, so total resident memory grows as they fill.
                            "private_mb": round((mem.rss - getattr(mem, "shared", 0)) / (1024 * 1024), 1),
                            "vms_mb": round(vms_mb, 1),
                            "threads": threads,
                            "fds": fds,
                        }
                        self._data[label].append(row)
                        rows_to_write.append(row)
                    except (psutil.NoSuchProcess, psutil.AccessDenied):
                        # The process has gone away: stop watching it. The lock is already held.
                        self._watched.pop(label, None)

            if rows_to_write:
                self._flush_rows(rows_to_write)

            if self._on_sample:
                for row in rows_to_write:
                    try:
                        self._on_sample(row)
                    except Exception:
                        pass

            self._stop_event.wait(self._interval)

    def _flush_rows(self, rows: list[dict]) -> None:
        if not rows:
            return
        file_exists = self._csv_path.exists()
        with open(self._csv_path, "a", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=["time", "label", "pid", "cpu_percent", "rss_mb", "private_mb", "vms_mb", "threads", "fds"])
            if not file_exists:
                writer.writeheader()
            for row in rows:
                writer.writerow(row)