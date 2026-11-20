"""Command line entry. Outside the sandbox it re-runs itself inside one and then checks that outside recorders were not disturbed; inside, it runs the suite and writes the reports."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import pathlib
import platform
import datetime
from dataclasses import dataclass
from revere_harness import sandbox
from revere_harness.steps import Run
from revere_harness.reporter import Reporter
from revere_harness.sampler import Sampler
from revere_harness.report import write_reports
from revere_harness.suites import cameras, discovery, smoke
from rich.console import Console

@dataclass
class Context:
    revere_dir: pathlib.Path
    out_dir: pathlib.Path
    reporter: Reporter
    sampler: Sampler
    keep: bool


SUITES = {"smoke": smoke.run, "cameras": cameras.run, "discovery": discovery.run}


def main(argv: list[str] | None = None) -> int:
    if argv is None:
        argv = sys.argv[1:]

    parser = argparse.ArgumentParser()
    parser.add_argument("suite", choices=list(SUITES.keys()))
    parser.add_argument("--revere-dir", default=None)
    parser.add_argument("--out", default=None)
    parser.add_argument("--keep", action="store_true", help="keep the recorder's data, secret and home directories after a passing run")
    args = parser.parse_args(argv)

    suite_name: str = args.suite
    revere_dir: pathlib.Path = resolve_revere_dir(args.revere_dir)
    out_dir: pathlib.Path = resolve_out_dir(args.out, suite_name)

    if not sandbox.in_sandbox():
        return run_host(revere_dir, out_dir, suite_name, args.keep)
    else:
        return run_sandboxed(revere_dir, out_dir, suite_name, args.keep)


def resolve_revere_dir(cli_path: str | None) -> pathlib.Path:
    if cli_path:
        return pathlib.Path(cli_path).resolve()
    repo_root = pathlib.Path(__file__).resolve().parents[3]
    candidate = repo_root / "build" / "stage" / "revere"
    if candidate.exists():
        return candidate.resolve()
    return pathlib.Path("/usr/local/revere").resolve()


def resolve_out_dir(cli_path: str | None, suite_name: str) -> pathlib.Path:
    if cli_path:
        return pathlib.Path(cli_path).resolve()
    harness_dir = pathlib.Path(__file__).resolve().parent.parent
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    return (harness_dir / "out" / f"{ts}-{suite_name}").resolve()


def run_host(revere_dir: pathlib.Path, out_dir: pathlib.Path, suite_name: str, keep: bool) -> int:
    os.makedirs(out_dir, exist_ok=True)
    before = sandbox.host_recorders()
    args = [sys.executable, "-m", "revere_harness", suite_name,
            "--revere-dir", str(revere_dir), "--out", str(out_dir)]
    if keep:
        args.append("--keep")
    
    try:
        code = sandbox.run_in_sandbox(args)
    except sandbox.SandboxError as e:
        print(str(e))
        return 3

    gone = sandbox.still_alive(before)
    if gone:
        pids = ", ".join(str(p["pid"]) for p in gone)
        Console().print(f"[red]host check: FAILED, process(es) that were running before the run are gone: {pids}[/red]")
        return 4
    
    print(f"host check: {len(before)} recorder or viewer process(es) were running before the run; all still running")
    return code


def run_sandboxed(revere_dir: pathlib.Path, out_dir: pathlib.Path, suite_name: str, keep: bool) -> int:
    try:
        sandbox.guard(check_network=False)
        sink = sandbox.isolate(out_dir / "syslog.log")
        sandbox.guard()
    except sandbox.SandboxError as e:
        print(f"sandbox guard: {e}", file=sys.stderr)
        return 3
    
    try:
        sandbox.bring_up_loopback()

        reporter = Reporter()
        csv_path = out_dir / "resources.csv"

        def on_sample(data: dict) -> None:
            if data.get("label") == "recorder":
                status = f"recorder cpu {data['cpu_percent']:.1f}% rss {data['rss_mb']:.0f} MB threads {data['threads']}"
                reporter.set_status(status)

        sampler = Sampler(csv_path, on_sample=on_sample)
        
        started = datetime.datetime.now().isoformat(timespec="seconds")
        
        meta = {
            "suite": suite_name,
            "started": started,
            "revere_dir": str(revere_dir),
            "python": sys.version.split()[0],
            "kernel": platform.release(),
            "keep": keep,
        }

        run_obj = Run(suite_name, reporter)
        ctx = Context(revere_dir=revere_dir, out_dir=out_dir, reporter=reporter, sampler=sampler, keep=keep)

        reporter.run_started(suite_name, out_dir)
        sampler.start()
        try:
            SUITES[suite_name](run_obj, ctx)
        except Exception as e:
            # Still write the report: the steps that did run are worth keeping.
            meta["suite_error"] = f"{type(e).__name__}: {e}"
        finally:
            sampler.stop()

        report_path = write_reports(out_dir, suite_name, run_obj.results, meta, sampler.summary())
        reporter.run_finished(run_obj.results, report_path)

        if run_obj.failed == 0 and "suite_error" not in meta:
            if not keep:
                recorder_dir = out_dir / "recorder"
                if recorder_dir.exists():
                    for name in ["data", "secret", "home"]:
                        dir_path = recorder_dir / name
                        if dir_path.exists():
                            shutil.rmtree(dir_path, ignore_errors=True)
                    reporter.note("removed the recorder's data, secret and home directories (use --keep to keep them)")
            else:
                reporter.note("kept the recorder's data, secret and home directories")
            return 0
        else:
            reporter.note("kept the recorder's data, secret and home directories for inspection")
            return 1
    finally:
        sink.stop()


if __name__ == "__main__":
    sys.exit(main())