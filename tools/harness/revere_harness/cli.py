"""Command line entry.

Outside the sandbox it re-runs itself inside one and then checks that outside
recorders were not disturbed; inside, it runs the suite and writes the reports.
The "web" command serves the local control page instead.
"""
from __future__ import annotations

import argparse
import datetime
import os
import pathlib
import platform
import re
import shutil
import sys
from dataclasses import dataclass
from typing import Callable

from rich.console import Console

from revere_harness import catalog, probe, sandbox
from revere_harness.report import write_reports
from revere_harness.reporter import Reporter
from revere_harness.sampler import Sampler
from revere_harness.steps import Run
from revere_harness.suites import cameras, discovery, recording, smoke, stress

HARNESS_DIR = pathlib.Path(__file__).resolve().parent.parent

SUITES = {
    "smoke": smoke.run,
    "cameras": cameras.run,
    "discovery": discovery.run,
    "recording": recording.run,
    "stress": stress.run,
}


@dataclass
class Context:
    revere_dir: pathlib.Path
    out_dir: pathlib.Path
    reporter: Reporter
    sampler: Sampler
    keep: bool
    params: dict
    snap: Callable[[str, str], None]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="run.sh", description="End-to-end test harness for the recorder.")
    parser.add_argument("suite", choices=sorted(SUITES) + ["web"], help='a suite, or "web" for the local control page')
    parser.add_argument("--revere-dir", default=None, help="folder holding the revere binary and its libraries")
    parser.add_argument("--out", default=None, help="output folder for this run")
    parser.add_argument("--keep", action="store_true",
                        help="keep the recorder's data, secret and home directories after a passing run")
    parser.add_argument("--param", action="append", default=[], metavar="NAME=VALUE", help="a suite parameter")
    parser.add_argument("--port", type=int, default=8090, help="port for the web page")
    parser.add_argument("--bind", default="127.0.0.1",
                        help="address the web page listens on; only a container that publishes the port to its "
                             "host's loopback should change it")
    args = parser.parse_args(argv)

    if args.suite == "web":
        # The page starts runs as separate processes, and each of those enters its own sandbox.
        from revere_harness import web
        return web.serve(HARNESS_DIR, out_base(), port=args.port, bind=args.bind,
                         revere_dir=pathlib.Path(args.revere_dir).resolve() if args.revere_dir else None)

    try:
        params = catalog.validate(args.suite, catalog.parse_param_args(args.param))
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    revere_dir = resolve_revere_dir(args.revere_dir)
    out_dir = resolve_out_dir(args.out, args.suite)
    if sandbox.in_sandbox():
        return run_sandboxed(revere_dir, out_dir, args.suite, params, args.keep)
    return run_host(revere_dir, out_dir, args)


def resolve_revere_dir(cli_path: str | None) -> pathlib.Path:
    if cli_path:
        return pathlib.Path(cli_path).resolve()
    candidate = HARNESS_DIR.parent.parent / "build" / "stage" / "revere"
    if candidate.exists():
        return candidate.resolve()
    return pathlib.Path("/usr/local/revere")


def out_base() -> pathlib.Path:
    """Where run folders go: tools/harness/out, or REVERE_HARNESS_OUT when the harness folder is read-only."""
    override = os.environ.get("REVERE_HARNESS_OUT")
    return pathlib.Path(override).resolve() if override else HARNESS_DIR / "out"


def resolve_out_dir(cli_path: str | None, suite_name: str) -> pathlib.Path:
    if cli_path:
        return pathlib.Path(cli_path).resolve()
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    return out_base() / f"{stamp}-{suite_name}"


def run_host(revere_dir: pathlib.Path, out_dir: pathlib.Path, args: argparse.Namespace) -> int:
    console = Console()
    out_dir.mkdir(parents=True, exist_ok=True)
    before = sandbox.host_recorders()
    command = [sys.executable, "-m", "revere_harness", args.suite,
               "--revere-dir", str(revere_dir), "--out", str(out_dir)]
    if args.keep:
        command.append("--keep")
    for pair in args.param:
        command += ["--param", pair]
    try:
        code = sandbox.run_in_sandbox(command)
    except sandbox.SandboxError as exc:
        console.print(f"[red]sandbox: {exc}[/red]", highlight=False)
        return 3

    gone = sandbox.still_alive(before)
    if gone:
        pids = ", ".join(str(p["pid"]) for p in gone)
        console.print(f"[red]host check: FAILED, process(es) that were running before the run are gone: {pids}[/red]",
                      highlight=False)
        return 4
    console.print(f"host check: {len(before)} recorder or viewer process(es) were running before the run; "
                  "all still running", highlight=False)
    return code


def run_sandboxed(revere_dir: pathlib.Path, out_dir: pathlib.Path, suite_name: str, params: dict, keep: bool) -> int:
    try:
        # /sys still shows the host's interfaces until isolate() mounts this namespace's own.
        sandbox.guard(check_network=False)
        sink = sandbox.isolate(out_dir / "syslog.log")
        sandbox.guard()
        sandbox.bring_up_loopback()
    except sandbox.SandboxError as exc:
        print(f"sandbox guard: {exc}", file=sys.stderr)
        return 3

    reporter = Reporter(events_path=out_dir / "events.jsonl")

    def on_sample(row: dict) -> None:
        reporter.sample(row)
        if row.get("label") == "recorder":
            reporter.set_status(f"recorder cpu {row['cpu_percent']:.1f}% rss {row['rss_mb']:.0f} MB "
                                f"threads {row['threads']}")

    sampler = Sampler(out_dir / "resources.csv", on_sample=on_sample)
    shots = 0

    def snap(caption: str, url: str) -> None:
        # A snapshot is a convenience for whoever watches the run; it must never fail a step.
        nonlocal shots
        try:
            shots += 1
            name = re.sub(r"[^A-Za-z0-9]+", "-", caption).strip("-")
            file = f"snapshots/{shots:04d}-{name}.jpg"
            if probe.snapshot(url, out_dir / file):
                reporter.snapshot(file, caption)
        except Exception:
            pass

    meta = {
        "suite": suite_name,
        "started": datetime.datetime.now().isoformat(timespec="seconds"),
        "revere_dir": str(revere_dir),
        "python": sys.version.split()[0],
        "kernel": platform.release(),
        "keep": keep,
        "params": params,
    }
    run = Run(suite_name, reporter)
    ctx = Context(revere_dir=revere_dir, out_dir=out_dir, reporter=reporter, sampler=sampler,
                  keep=keep, params=params, snap=snap)
    interrupted = False
    try:
        reporter.run_started(suite_name, out_dir)
        sampler.start()
        try:
            SUITES[suite_name](run, ctx)
        except KeyboardInterrupt:
            # The suites stop their own processes in their finally blocks.
            interrupted = True
            meta["suite_error"] = "interrupted"
        except Exception as exc:
            # Still write the report: the steps that did run are worth keeping.
            meta["suite_error"] = f"{type(exc).__name__}: {exc}"
        finally:
            sampler.stop()

        passed = run.failed == 0 and "suite_error" not in meta
        report_path = write_reports(out_dir, suite_name, run.results, meta, sampler.summary())
        if passed and not keep:
            for name in ("data", "secret", "home"):
                shutil.rmtree(out_dir / "recorder" / name, ignore_errors=True)
            reporter.note("removed the recorder's data, secret and home directories (use --keep to keep them)")
        else:
            reporter.note("kept the recorder's data, secret and home directories for inspection")
        reporter.run_finished(run.results, report_path)
    finally:
        sink.stop()
        reporter.close()

    if interrupted:
        return 130
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
