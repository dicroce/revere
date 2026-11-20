# Revere Recorder Test Harness

This is an end-to-end test harness for the Revere recorder. It includes fake cameras, two new suites, and a smoke suite. Still to come are recording checks, API tests, desktop UI tests, and stress runs.

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

## Exit Codes

- 0: All steps passed.
- 1: A step failed.
- 3: The sandbox could not be set up or its guard refused to run.
- 4: A recorder or viewer that was running outside the sandbox before the run is gone after it.

## Sandbox and Safety

The recorder uses fixed ports. Port 8088 is for the API. Port 8554 is for RTSP. Port 3702 is for discovery. The machine running the tests may also run a real recorder. Therefore, every run happens inside a new user, network, mount, and PID namespace made with `unshare`.

Inside the sandbox, tests have their own loopback and ports. There is no route to the outside network. A test cannot reach a real camera or a cloud service. Processes outside are invisible.

The code follows strict safety rules. First, the harness only signals processes it started itself. It uses their process handle. It never signals by name. Second, before anything starts, a guard checks that it is PID 1 of its own namespace. It also checks that no `revere` or `vision` process is visible. The run stops if this check fails. Third, after the run, outside the sandbox, it checks that every recorder or viewer that was running before is still running.

The sandbox mounts empty tmpfs directories over /run/dbus, /run/user, /tmp/.X11-unix and /run/systemd/journal. It removes the D-Bus, display, audio and SSH agent variables from the environment (DBUS_SESSION_BUS_ADDRESS, DISPLAY, WAYLAND_DISPLAY, XAUTHORITY, XDG_RUNTIME_DIR and similar). Nothing a test starts can talk to the desktop session by accident. A later visible-UI mode will pass in only what it needs.

It mounts its own sysfs. The guard stops the run unless the only network interface is lo. Later guard calls allow lo plus the bridge (br0) and camera links (vc...) the harness itself creates.

The recorder logs through syslog. A private log socket inside the sandbox collects those lines into `syslog.log` in the output folder, so they do not land in the machine's journal.

Each run gets fresh temporary data, secret, and home directories under the output folder. It never touches a real installation's data.

## Output

Terminal output shows one PASS, FAIL, or SKIP line per step with its time. Failure details appear underneath. A running summary line shows counts, elapsed time, and the recorder's CPU, memory, and thread count.

The output folder contains:
- `report.md`: Summary, steps table, and resource table.
- `results.json`: Raw results.
- `junit.xml`: JUnit compatible XML.
- `resources.csv`: Time, label, pid, cpu_percent, rss_mb, vms_mb, threads, and fds. One row per process per second.
- `recorder/recorder.log`: The recorder log.
- `syslog.log`: The recorder's own log lines.
- Temporary directories for the recorder are present only after a failed run or with `--keep`.
- `cameras/<name>.log`: Log output from each fake camera.

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
- `suites/smoke.py`: Smoke suite definition.
- `suites/cameras.py`: Cameras suite definition.
- `suites/discovery.py`: Discovery suite definition.

## Adding a Suite

Write `suites/<name>.py` with a `run(run, ctx)` function. Wrap each check in `with run.step("..."):`. Use `run.check(condition, message)` for assertions. Use `run.skip(reason)` to skip steps. Register the suite in `SUITES` in cli.py.

## Build Note

On systems with recent PipeWire, the bundled SDL2 may fail to compile its PipeWire audio backend. Configure with `-DSDL_PIPEWIRE=OFF` to fix this.