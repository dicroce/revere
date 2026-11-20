from __future__ import annotations

import contextlib
import dataclasses
import time
from typing import TYPE_CHECKING, Iterator

if TYPE_CHECKING:
    from revere_harness.reporter import Reporter


@dataclasses.dataclass
class StepResult:
    suite: str
    name: str
    status: str
    seconds: float
    detail: str = ""


class StepFailed(AssertionError):
    pass


class StepSkipped(Exception):
    pass


class Run:
    def __init__(self, suite: str, reporter: Reporter) -> None:
        self.suite = suite
        self.reporter = reporter
        self.results: list[StepResult] = []

    @contextlib.contextmanager
    def step(self, name: str) -> Iterator[None]:
        start = time.monotonic()
        self.reporter.step_started(name)
        try:
            yield
        except StepSkipped as exc:
            self._finish(name, "skipped", start, detail=str(exc))
        except (KeyboardInterrupt, SystemExit) as exc:
            self._finish(name, "failed", start, detail=f"{type(exc).__name__}: {exc}")
            raise
        except Exception as exc:
            self._finish(name, "failed", start, detail=f"{type(exc).__name__}: {exc}")
        else:
            self._finish(name, "passed", start)

    def _finish(self, name: str, status: str, start: float, detail: str = "") -> None:
        elapsed = time.monotonic() - start
        result = StepResult(
            suite=self.suite,
            name=name,
            status=status,
            seconds=elapsed,
            detail=detail,
        )
        self.results.append(result)
        self.reporter.step_finished(result)

    def check(self, condition: bool, message: str) -> None:
        if not condition:
            raise StepFailed(message)

    def skip(self, reason: str) -> None:
        raise StepSkipped(reason)

    @property
    def failed(self) -> int:
        return sum(1 for r in self.results if r.status == "failed")

    @property
    def passed(self) -> int:
        return sum(1 for r in self.results if r.status == "passed")

    @property
    def skipped(self) -> int:
        return sum(1 for r in self.results if r.status == "skipped")