# Revere Recorder Test Harness

This is an end-to-end test harness for the Revere recorder. It includes fake cameras, a recording suite, a stress suite and a local web page. API tests and desktop UI tests are still to come.

## Platform Requirements

This harness works on Linux only. It requires Python 3.11 or newer. You need GStreamer 1.x with gst-rtsp-server, the good, bad and libav plugin sets, the openh264 and x265 encoder plugins, PyGObject, `ffprobe` and `ffmpeg`, `nsenter`, `tc` and `mount`. The kernel must have unprivileged user namespaces enabled. Install the Python packages listed in requirements.txt. These include rich and psutil. No root privileges are needed.

## Setup

Build Revere first. Then install it to a local folder:

```bash
cmake --install build --prefix build/stage
```

The harness looks for the recorder in `build/stage/revere` by default. It falls back to `/usr/local/revere`. Use `--revere-dir DIR` to override this path.

Create a virtual environment and install dependencies:

```bash
python3 -m venv --system-site-packages tools/harness/.venv
tools/harness/.venv/bin/pip install -r tools/harness/requirements.txt
```

The run script uses `.venv` if it exists. Otherwise, it uses system python3.

## Running Tests

Run the smoke suite with:

```bash
tools/harness/run.sh smoke
```

Use `--revere-dir DIR` to specify the recorder location. Use `--out DIR` to set the output directory. The default is `tools/harness/out/<date-time>-<suite>`. Use `--keep` to keep the recorder's data, secret and home directories after a passing run. After a failed run they are always kept for inspection.

Use `--param NAME=VALUE` (repeatable) to pass suite parameters. Example:

```bash
tools/harness/run.sh recording --param long_drop=60
tools/harness/run.sh stress --param scenario=reconnect_storm --param cameras=8 --param minutes=5
```

Unknown or out-of-range parameters are refused before anything starts. Exit code 2 means a bad parameter; 130 means the run was interrupted.

## Exit Codes

- 0: All steps passed.
- 1: A step failed.
- 2: A bad parameter was provided.
- 3: The sandbox could not be set up or its guard refused to run.
- 4: A recorder or viewer that was running outside the sandbox before the run is gone after it.
- 130: The run was interrupted.

## Sandbox and Safety

The recorder uses fixed ports. Port 8088 is for the API. Port 8554 is for RTSP. Port 3702 is for discovery. The machine running the tests may also run a real recorder. Therefore, every run happens inside a new user, network, mount, and PID namespace made with `unshare`.

Inside the sandbox, tests have their own loopback and ports. There is no route to the outside network. A test cannot reach a real camera or a cloud service. Processes outside are invisible.

The code follows strict safety rules. First, the harness only signals processes it started itself. It uses their process handle. It never signals by name. Second, before anything starts, a guard checks that it is PID 1 of its own namespace. It also checks that no `revere` or `vision` process is visible. The run stops if this check fails. Third, after the run, outside the sandbox, it checks that every recorder or viewer that was running before is still running.

The sandbox mounts empty tmpfs directories over /run/dbus, /run/user, /tmp/.X11-unix and /run/systemd/journal. It removes the D-Bus, display, audio and SSH agent variables from the environment (DBUS_SESSION_BUS_ADDRESS, DISPLAY, WAYLAND_DISPLAY, XAUTHORITY, XDG_RUNTIME_DIR and similar). Nothing a test starts can talk to the desktop session by accident. A later visible-UI mode will pass in only what it needs.

It mounts its own sysfs. The guard stops the run unless the only network interface is lo. Later guard calls allow lo plus the bridge (br0) and camera links (vc...) the harness itself creates.

The recorder logs through syslog. A private log socket inside the sandbox collects those lines into `syslog.log` in the output folder, so they do not land in the machine's journal. The private log socket is also bind-mounted over /dev/log when the machine has a syslog socket there, with a self-check that a test line reaches the private log.

Each run gets fresh temporary data, secret, and home directories under the output folder. It never touches a real installation's data. The guard now compares the visible network interfaces with the exact set the harness created (not a name pattern). URLs with credentials, Bearer tokens and password or token values are masked in the terminal output, `events.jsonl` and all report files.

## Output

Terminal output shows one PASS, FAIL, or SKIP line per step with its time. Failure details appear underneath. A running summary line shows counts, elapsed time, and the recorder's CPU, memory, and thread count.

The output folder contains:
- `report.md`: Summary, steps table, and resource table.
- `results.json`: Raw results.
- `junit.xml`: JUnit compatible XML.
- `resources.csv`: Time, label, pid, cpu_percent, rss_mb, vms_mb, private_mb, threads, and fds. One row per process per second.
- `recorder/recorder.log`: The recorder log.
- `syslog.log`: The recorder's own log lines.
- `events.jsonl`: One JSON object per line: run_started, step_started, step_finished, note, sample, snapshot, run_finished.
- `snapshots/`: JPEG frames taken at key moments.
- Temporary directories for the recorder are present only after a failed run or with `--keep`.
- `cameras/<name>.log`: Log output from each fake camera.
- For runs started from the web page, `console.log` is also created.

## Suites

The smoke suite verifies:
- Sandbox hides outside processes.
- Recorder starts headless.
- GET /version returns build details.
- GET /auth_status is open.
- GET /cameras without a token is refused with 401.
- Recorder keeps running.
- Recorder stops cleanly on SIGINT.
- No process left behind.

The cameras suite starts six fake cameras. It uses H.264, H.264 with AAC and a substream, H.264 with MuLaw, H.265, H.265 with AAC and a substream, and H.264 without auth. It checks every stream with ffprobe. It checks password handling, multicast and unicast discovery. It proves each fault control has its effect. No recorder is involved.

The discovery suite starts four fake cameras and a headless recorder. It checks that the recorder lists every camera as discovered with the right address. It checks that each camera saw the recorder's probe.

The recording suite (about 6 minutes) adds four fake cameras through the same API calls the web UI makes. Checks: each receives video; the pre-allocated recording file is written in place; the recorded range grows; one continuous segment; an outage shorter than the recorder's dead-stream timeout does not restart the stream; an unplugged cable, a stopped RTSP server and a stalled stream each show as a gap that starts when the data stopped; other cameras keep recording; a still picture gives no motion events and a scripted 15 second scroll gives exactly one with the right start and end; removing a camera stops its recording. Parameters: `long_drop` (seconds, default 40) and `short_drop` (seconds, default 8).

The stress suite runs one scenario per run, chosen with the `scenario` parameter.

| Scenario | What it does |
| :--- | :--- |
| `many_cameras` | Records from N cameras for the given minutes and checks every camera every 15 seconds |
| `soak` | Is the same as many_cameras, meant for long durations |
| `reconnect_storm` | Unplugs every camera for 30 seconds and plugs them back, again and again, and checks all recover and each shows one gap per cycle |
| `add_remove` | Adds and removes every camera in a loop and checks none is left assigned |
| `api_hammer` | Runs parallel clients sending a mix of valid requests, bad requests and a wrong token, and checks the answers and that recording continues |
| `disk_full` | Puts the recorder's data on a 96 MB tmpfs and adds cameras until one is refused, then checks the refusal is clean and the cameras that fit keep recording |

Parameters: `scenario`, `cameras` (default 8), `minutes` (default 3), `resolution` (640x360, 1280x720 or 1920x1080), `fps` (default 10), `bitrate_kbps` (default 800), `clients` (default 8).

Every stress scenario ends by checking the recorder is still running and stops cleanly, and by a growth check on the recorder's private memory, thread count and open file descriptors. The check looks only at the time the scenario's load was running, and only at the second half of that, so set-up and settling are left out. A line is fitted to each measure; a measure fails the step when it grows faster than 2 MB, 0.5 threads or 0.5 descriptors a minute and also ends clearly above where it began (medians of the first and last quarter). "Clearly" is half of what growth at that rate would add over the judged time, and at least 4 MB, 2 threads or 4 descriptors, so a few descriptors coming and going do not count and a longer run catches slower growth: growth of 3 MB a minute is caught from 4 minutes of load, and of 2 MB a minute from about 30. Private memory is resident memory minus file-backed pages; the recorder maps its pre-allocated files, so total resident memory grows by design. A second half under 75 seconds is not judged; use `minutes=3` or more for the check to count, and a soak of 30 minutes or more to see slow growth. A reconnect storm is judged only from `minutes=10`: the recorder's memory settles over the first ten minutes of a storm (measured: 388 MB early, 414 MB at 14 minutes, level at about 429 MB from 28 minutes), and a shorter storm cannot tell that from growth.

Every fake camera encodes its own video, so many cameras cost the host CPU as well as the recorder. Measured on one desktop machine, a camera at 640x360 and 10 frames a second used about 4% of one core; 1920x1080 has nine times the pixels and costs roughly in proportion, so 64 cameras at 1080p need a machine with many free cores. When the host cannot keep up, the cameras themselves fall behind and the run reports lagging cameras that are not the recorder's doing. The resource table in the report shows what one camera and the recorder used.

## Findings

When a check shows recorder behaviour that looks like a defect but is outside what the step asserts, the harness prints a note starting with FINDING and, where it applies, skips the step with the reason. Findings appear in the terminal and in events.jsonl.

## Web page

`tools/harness/run.sh web` (options `--port`, default 8090, and `--revere-dir`, used for every run it starts) serves a control page and prints its address, which includes a secret token. The page lists the suites and their parameters, starts and stops a run (one at a time), and shows the run live: counts and elapsed time, a tile per watched process with CPU, memory, threads and a memory sparkline, every step as it passes, fails or is skipped, notes and findings, and snapshots taken along the way. Past runs are listed with links to their report and logs and can be replayed. Safety: the server listens on 127.0.0.1 only; every request needs the token (from the address, then a cookie); requests with another Host header are refused; it only signals the run it started; each run enters its own sandbox exactly as from the command line. The page can start processes on the machine, so do not expose the port.

Requests that change anything (start, stop) are accepted only from the page's own origin, so a page served from another port on the same machine cannot start runs. Text files the page serves from a run's folder are masked the same way as the reports.

`tests/` holds unit tests for the masking and for the page's access checks; run them with `python -m pytest tests` from `tools/harness` (they start no recorder).

## Fake Cameras

Each fake camera is its own process in its own network namespace. It joins a bridge (10.77.0.1/24) inside the sandbox by a veth pair. It has its own IP address (10.77.0.<10 + index>) and MAC address, like a camera on a LAN. It serves RTSP on port 554 at /main and optionally /sub (640x360). It answers ONVIF on port 80. It handles WS-Discovery probes on UDP 3702. It handles GetSystemDateAndTime, GetDeviceInformation, GetCapabilities, GetServices, GetProfiles, GetProfile, GetStreamUri, and GetNetworkInterfaces over SOAP 1.1 or 1.2 with WS-Security digest or text passwords. Other requests get an ActionNotSupported fault. Video is H.264 (openh264) or H.265 (x265). Audio is none, AAC or MuLaw. RTSP uses basic auth, or no auth when the user name is empty. A running-time stamp is drawn at the top left of the picture.

## Fault Controls

| Control | What it does |
| :--- | :--- |
| `set_ip(ip)` | Re-addresses the camera |
| `set_mac(mac)` | Changes its hardware address |
| `unplug()` / `plug()` | Takes its link down and up |
| `slow(delay_ms, loss_percent, rate_kbit)` / `normal()` | Adds delay, loss or a rate limit with tc netem |
| `rtsp(on)` | Stops or starts only the RTSP server |
| `onvif(on)` | Stops or starts only the ONVIF side |
| `stall(on)` | Pauses the stream while keeping sessions open |
| `scene(name)` | Switches the picture between "static" (still colour bars), "moving" (a small bouncing ball), "scrolling" (the whole picture scrolls, which the recorder's motion detection picks up) and "noise" (random noise). |

## Code Layout

- `run.sh`: Entry point script.
- `revere_harness/cli.py`: Entry logic. Re-runs itself inside the sandbox.
- `sandbox.py`: Sandbox management.
- `recorder.py`: Recorder control. Gained `token()`, which reads the local API token of the throwaway recorder.
- `net.py`: Bridge and per-camera network plumbing.
- `camera.py`: Camera handle and fault controls.
- `probe.py`: ffprobe, discovery probe and picture-change helpers.
- `fakecam/`: The camera process. Contains `rtsp.py`, `onvif.py`, and `__main__.py`.
- `steps.py`: Steps and results handling.
- `reporter.py`: Terminal output formatting.
- `sampler.py`: Resource sampling.
- `report.py`: Report file generation.
- `api.py`: Client for the recorder's local API.
- `catalog.py`: Suites and their parameters.
- `suites/smoke.py`: Smoke suite definition.
- `suites/cameras.py`: Cameras suite definition.
- `suites/discovery.py`: Discovery suite definition.
- `growth.py`: Growth check.
- `suites/recording.py`: Recording suite definition.
- `suites/stress.py`: Stress suite definition.
- `web.py` and `web_page.html`: The control page.
- `redact.py`: Masks credentials in everything written to the terminal, the event log and the reports.

## Adding a Suite

Write `suites/<name>.py` with a `run(run, ctx)` function. Wrap each check in `with run.step("..."):`. Use `run.check(condition, message)` for assertions. Use `run.skip(reason)` to skip steps. Also add an entry to `catalog.py`. Parameters arrive in `ctx.params`. `ctx.snap(caption, rtsp_url)` saves a snapshot. `ctx.reporter.note(text)` adds a note. Register the suite in `SUITES` in cli.py.

## Build Note

On systems with recent PipeWire, the bundled SDL2 may fail to compile its PipeWire audio backend. Configure with `-DSDL_PIPEWIRE=OFF` to fix this.