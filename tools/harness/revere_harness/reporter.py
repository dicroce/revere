from __future__ import annotations

import json
import pathlib
import threading
import time
from typing import Optional

from revere_harness.steps import StepResult
from rich.console import Console
from rich.markup import escape
from rich.table import Table


class Reporter:
    def __init__(self, console: Optional[Console] = None, events_path: pathlib.Path | None = None) -> None:
        self.console = console or Console()
        self.suite: str = ""
        self.out_dir: pathlib.Path = pathlib.Path(".")
        self.passed_count: int = 0
        self.failed_count: int = 0
        self.skipped_count: int = 0
        self.start_time: float = 0.0
        self.status_text: str = ""
        self._events_path = events_path
        self._events_file = None
        self._events_lock = threading.Lock()

    def _elapsed(self) -> str:
        if self.start_time == 0.0:
            return "0:00"
        elapsed = time.monotonic() - self.start_time
        minutes = int(elapsed // 60)
        seconds = int(elapsed % 60)
        return f"{minutes}:{seconds:02d}"

    def _event(self, type: str, **fields) -> None:
        if self._events_path is None:
            return
        record = {"t": time.time(), "type": type, **fields}
        with self._events_lock:
            if self._events_file is None:
                self._events_file = open(self._events_path, "a")
            self._events_file.write(json.dumps(record) + "\n")
            self._events_file.flush()

    def run_started(self, suite: str, out_dir: pathlib.Path) -> None:
        self.suite = suite
        self.out_dir = out_dir.resolve()
        self.passed_count = 0
        self.failed_count = 0
        self.skipped_count = 0
        self.start_time = time.monotonic()
        self.status_text = ""
        self.console.print(f"[bold]Running suite:[/bold] {suite}")
        self._event("run_started", suite=suite, out_dir=str(self.out_dir))

    def step_started(self, name: str) -> None:
        self._event("step_started", name=name)

    def step_finished(self, result: StepResult) -> None:
        elapsed_str = self._elapsed()

        if result.status == "passed":
            color = "green"
            symbol = "PASS"
            self.passed_count += 1
        elif result.status == "failed":
            color = "red"
            symbol = "FAIL"
            self.failed_count += 1
        else:
            color = "yellow"
            symbol = "SKIP"
            self.skipped_count += 1

        self.console.print(f"  [{color}]{symbol}[/{color}] {escape(result.name)} ({result.seconds:.2f}s)", highlight=False)
        if result.detail:
            for line in result.detail.splitlines():
                self.console.print(f"    [dim]{escape(line)}[/dim]", highlight=False)

        summary = f"  passed {self.passed_count}  failed {self.failed_count}  skipped {self.skipped_count}  elapsed {elapsed_str}  {self.status_text}"
        self.console.print(f"[dim]{escape(summary)}[/dim]", highlight=False, soft_wrap=True)
        self._event("step_finished", name=result.name, status=result.status, seconds=round(result.seconds, 3), detail=result.detail or "")

    def set_status(self, text: str) -> None:
        self.status_text = text

    def note(self, text: str) -> None:
        self.console.print(f"  [dim]note:[/dim] {escape(text)}", highlight=False)
        self._event("note", text=text)

    def run_finished(self, results: list[StepResult], report_path: pathlib.Path) -> None:
        elapsed_str = self._elapsed()

        table = Table(title=f"{self.suite}: finished")
        table.add_column("Metric", style="cyan")
        table.add_column("Value", style="green")
        table.add_row("Passed", str(self.passed_count))
        if self.failed_count != 0:
            table.add_row("Failed", f"[red]{self.failed_count}[/red]")
        else:
            table.add_row("Failed", str(self.failed_count))
        table.add_row("Skipped", str(self.skipped_count))
        table.add_row("Elapsed", elapsed_str)
        self.console.print(table)
        self.console.print(f"report: {report_path}", soft_wrap=True)
        
        exit_code = 0 if self.failed_count == 0 else 1
        self._event("run_finished", passed=self.passed_count, failed=self.failed_count, skipped=self.skipped_count, exit_code=exit_code)

    def sample(self, row: dict) -> None:
        self._event("sample", label=row.get("label"), cpu_percent=row.get("cpu_percent"), rss_mb=row.get("rss_mb"), threads=row.get("threads"), fds=row.get("fds"))

    def snapshot(self, file: str, caption: str) -> None:
        self._event("snapshot", file=file, caption=caption)
        self.console.print(f"  [dim]snapshot: {caption} -> {file}[/dim]", highlight=False)

    def close(self) -> None:
        if self._events_file is not None:
            self._events_file.close()
            self._events_file = None