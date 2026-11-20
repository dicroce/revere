from __future__ import annotations

import pathlib
import time
from typing import Optional

from revere_harness.steps import StepResult
from rich.console import Console
from rich.markup import escape
from rich.table import Table


class Reporter:
    def __init__(self, console: Optional[Console] = None) -> None:
        self.console = console or Console()
        self.suite: str = ""
        self.out_dir: pathlib.Path = pathlib.Path(".")
        self.passed_count: int = 0
        self.failed_count: int = 0
        self.skipped_count: int = 0
        self.start_time: float = 0.0
        self.status_text: str = ""

    def _elapsed(self) -> str:
        if self.start_time == 0.0:
            return "0:00"
        elapsed = time.monotonic() - self.start_time
        minutes = int(elapsed // 60)
        seconds = int(elapsed % 60)
        return f"{minutes}:{seconds:02d}"

    def run_started(self, suite: str, out_dir: pathlib.Path) -> None:
        self.suite = suite
        self.out_dir = out_dir.resolve()
        self.passed_count = 0
        self.failed_count = 0
        self.skipped_count = 0
        self.start_time = time.monotonic()
        self.status_text = ""
        self.console.print(f"[bold]Running suite:[/bold] {suite}")

    def step_started(self, name: str) -> None:
        pass

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

    def set_status(self, text: str) -> None:
        self.status_text = text

    def note(self, text: str) -> None:
        self.console.print(f"  [dim]note:[/dim] {escape(text)}", highlight=False)

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