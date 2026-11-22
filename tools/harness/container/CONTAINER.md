# Running the harness in a container

## What this is

The harness needs Linux: it builds its own user, network, mount and PID namespaces for every run. This folder runs it on macOS, or on any machine with a Docker engine, inside an Ubuntu 24.04 container. The image holds the recorder built for Linux and what the harness and its fake cameras need. The harness itself is not in the image: it is mounted from the checkout, so editing it needs no rebuild.

## Requirements

- A Docker engine: Docker Desktop, or Rancher Desktop with the dockerd (moby) engine. When `docker` is not on the PATH the script looks for Rancher Desktop's copy in `~/.rd/bin`.
- Disk: the image is about 1.2 GB. Building it needs about 4 GB more for the build cache, which `mac prune` gives back afterwards.
- Time: the first build compiles the recorder. It took about 5 minutes on a 16-core desktop; expect longer on a laptop. Later builds only redo what changed, and a change to the harness needs no build at all.

## Commands

Run these from the repository root.

| Command | What it does |
| :--- | :--- |
| `tools/harness/run.sh mac build` | Builds the image. The first run of any other command does this by itself. |
| `tools/harness/run.sh mac smoke` | Runs one suite. |
| `tools/harness/run.sh mac cameras discovery recording` | Runs several suites in turn and prints one line per suite at the end. |
| `tools/harness/run.sh mac recording -- --param long_drop=30` | Everything after `--` goes to every suite. |
| `tools/harness/run.sh mac web` | Serves the control page; see below. Stop it with Ctrl-C. |
| `tools/harness/run.sh mac test` | The harness's unit tests. |
| `tools/harness/run.sh mac shell` | A shell in the container. |
| `tools/harness/run.sh mac size` | The image's size and what Docker is using. |
| `tools/harness/run.sh mac prune` | Drops the build cache and keeps the image. |
| `tools/harness/run.sh mac clean` | Removes the container, the image and the build cache. |

`container` works in place of `mac`. One container runs at a time.

## Where results go

Reports, logs, snapshots and the event log of every run are copied to `tools/harness/out` in the checkout, while the run is going and once more at the end. The recorder's own databases stay on the container's disk: a folder shared with the host is slow for them, and they are deleted after a passing run anyway. Run folders are named by the container's clock, which is UTC.

## The web page

`mac web` publishes the page on `http://127.0.0.1:8090` and prints its address with the secret token; open that address in a browser on the same machine. `HARNESS_PORT=8091 tools/harness/run.sh mac web` uses another port. The page lists the suites, starts and stops runs and shows them live, with the snapshots taken along the way.

## Safety

- The container never uses the host's network. The recorder under test and its fake cameras are on networks that exist only inside the container.
- Only the page's port is published, only for `mac web`, and only on the host's loopback address. The recorder's own ports (8088, 8443, 8554 and 3702) are never published, so a real recorder on the host is not disturbed, and the script refuses to use one of them for the page.
- The container is not run privileged. It gets the SYS_ADMIN and NET_ADMIN capabilities and unconfined seccomp, AppArmor and system paths, which the harness needs to build its namespaces, mounts and virtual network links. It gets none of the host's devices.
- On the host it reads the checkout's `tools/harness` folder and writes only to `tools/harness/out`.
- The page listens on all of the container's addresses, because that is the only way a published port can reach it; the harness refuses that setting anywhere but in this container, and the token, Host and Origin checks apply as usual.

## Architecture

The image is built for the machine it is built on: arm64 on Apple silicon, amd64 on Intel. Nothing is emulated.

## Disk space and cleaning up

`mac size` shows what is in use. After a build, `mac prune` frees the build cache (about 4 GB) and keeps the 1.2 GB image. `mac clean` removes the image too; the next run rebuilds it. Neither touches `tools/harness/out`; delete that folder yourself when you no longer want the reports.

## Limits

- No object detection plugin is built into the image, so a suite that needs one skips.
- The desktop app runs offscreen; what it showed appears as pictures in the report and on the web page.
- Timing is that of a container on a laptop: with many cameras the fake cameras may lag before the recorder does.
