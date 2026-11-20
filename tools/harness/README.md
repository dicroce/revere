# Revere Recorder Test Harness

This is an end-to-end test harness for the Revere recorder. It is built in phases. This first part provides a skeleton. It includes the sandbox, starting and stopping a headless recorder, live terminal output, reports, and a smoke suite. Later parts will add fake cameras, recording checks, API tests, desktop UI tests, and stress runs.

## Requirements

This harness works on Linux only. You need Python 3.11 or newer. You also need `unshare` from util-linux and `ip` from iproute2. The kernel must have unprivileged user namespaces enabled. Do not run as root. Install the Python packages listed in requirements.txt. These include rich and psutil.

## Setup

Build Revere first. Then install it to a local folder. Run this command:

```bash
cmake --install build --prefix build/stage
```

The harness looks for the recorder in `build/stage/revere` by default. It then checks `/usr/local/revere`. Use `--revere-dir DIR` to override this path.

Create a virtual environment and install dependencies:

```bash
python3 -m venv tools/harness/.venv
tools/harness/.venv/bin/pip install -r tools/harness/requirements.txt
```

The run script uses `.venv` if it exists. Otherwise, it uses system python3.

## Running Tests

Run the smoke suite with this command:

```bash
tools/harness/run.sh smoke
```

You can pass `--revere-dir DIR` to specify the recorder location. Use `--out DIR` to set the output directory. The default is `tools/harness/out/<date-time>-<suite>`.

## Exit Codes

The script returns specific exit codes:

- 0: All steps passed.
- 1: A step failed.
- 3: The sandbox could not be set up or its guard refused to run.
- 4: A recorder or viewer that was running outside the sandbox before the run is gone after it.

## The Sandbox

The recorder uses fixed ports. Port 8088 is for the API. Port 8554 is for RTSP. Port 3702 is for discovery. The machine running the tests may also be running a real recorder. Therefore, every run happens inside a new user, network, mount, and PID namespace made with `unshare`.

Inside the sandbox, the tests have their own loopback and ports. There is no route to the outside network. A test cannot reach a real camera or a cloud service. Processes outside are invisible.

## Safety Rules

The code follows strict safety rules:

1. The harness only signals processes it started itself. It uses their process handle, never by name.
2. Before anything starts, a guard checks that it is PID 1 of its own namespace. It also checks that no `revere` or `vision` process is visible. It stops the run if not.
3. After the run, outside the sandbox, it checks that every recorder or viewer that was running before is still running.

Each run gets fresh temporary data, secret, and home directories under the output folder. It never touches a real installation's data.

## Output

The terminal shows one PASS, FAIL, or SKIP line per step with its time. Failure details appear underneath. A running summary line shows counts, elapsed time, and the recorder's CPU, memory, and thread count.

The output folder contains:

- `report.md`: Summary, steps table, and resource table.
- `results.json`: Raw results.
- `junit.xml`: JUnit compatible XML.
- `resources.csv`: Time, label, pid, cpu_percent, rss_mb, vms_mb, threads, and fds. One row per process per second.
- `recorder/recorder.log`: The recorder log.
- The recorder's temporary directories.

## Smoke Suite Steps

The smoke suite verifies:

- The sandbox hides outside processes.
- The recorder starts headless.
- GET /version returns build details.
- GET /auth_status is open.
- GET /cameras without a token is refused with 401.
- The recorder keeps running.
- The recorder stops cleanly on SIGINT.
- No process is left behind.

## Code Layout

- `run.sh`: Entry point script.
- `revere_harness/cli.py`: Entry logic. It re-runs itself inside the sandbox.
- `sandbox.py`: Sandbox management.
- `recorder.py`: Recorder control.
- `steps.py`: Steps and results handling.
- `reporter.py`: Terminal output formatting.
- `sampler.py`: Resource sampling.
- `report.py`: Report file generation.
- `suites/smoke.py`: Smoke test definitions.

## Adding a Suite

Write `suites/<name>.py` with a `run(run, ctx)` function. Wrap each check in `with run.step("..."):`. Use `run.check(condition, message)` and `run.skip(reason)`. Register the suite in `SUITES` in cli.py.

## Building on PipeWire Systems

If you build on a system with recent PipeWire, the bundled SDL2 may fail to compile its PipeWire audio backend. Configure with `-DSDL_PIPEWIRE=OFF` to fix this.