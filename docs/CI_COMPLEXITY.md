# Why Building Revere in CI Is Complicated

This document explains, concretely, why automated multi-platform release builds
for Revere are harder than a typical app. It exists because the difficulty is
not obvious from the source tree — most of it only surfaces when you try to
produce a **distributable** artifact that runs on a clean machine (no dev tools,
no Homebrew, no pre-installed libraries).

The short version: Revere is a native C++ desktop app that pulls in several
**large, native, platform-specific multimedia dependencies** (OpenCV, GStreamer,
FFmpeg, NCNN, SDL2), loads code at runtime via **plugins (`dlopen`)**, ships
**two apps plus separately-installed cloud plugin**, and targets **three OSes
that each package native apps completely differently**. Every one of those
traits adds a class of problem that a pure-managed-language or single-platform
app never hits.

---

## 1. Heavy native dependencies that aren't "just install a package"

Revere links OpenCV, GStreamer, FFmpeg, SDL2, and (optionally) NCNN. These are
big, native, and versioned, and each platform sources them differently:

- **Windows:** no system package manager for these. We pre-build/download them
  once and host a "devkit" bundle (`revere-deps` repo, ~1 GB) that CI restores
  via cache + sha256 verification. The OpenCV version is even pinned in CMake
  (`opencv_world4120`).
- **Linux:** OpenCV/FFmpeg/GStreamer from apt, but **NCNN has no usable apt
  package** so CI builds it from source and caches it.
- **macOS:** all via Homebrew — but Homebrew versions float, and Homebrew splits
  things across many formulae (see GStreamer below).

Consequence: there is no single "install dependencies" step that works
everywhere. Each platform needs its own provisioning strategy, and a dependency
version bump can break one platform while leaving the others fine.

## 2. The "works on my machine" trap is the default failure mode

The dev machine already has everything installed and configured, so the app runs
even when it is **not actually self-contained**. The defects only appear when a
*clean* machine runs the packaged artifact. Real examples found while setting up
CI:

- The macOS `.dmg` looked fine in dev but only bundled *project* dylibs — the
  heavy Homebrew deps (OpenCV/FFmpeg/GStreamer) still resolved via
  `/opt/homebrew/...`, so the `.dmg` only ran on machines that had Homebrew.
- A missing `~/Documents` on a fresh Linux account aborted startup
  (`mkdir` vs `mkdir -p`).
- A stale on-disk encryption key from earlier builds caused credential-decrypt
  failures that were invisible until a clean run.

Consequence: CI is the first time the app is ever tested as a real
distributable, so it surfaces a backlog of latent, user-affecting bugs that have
nothing to do with CI itself.

## 3. Runtime-loaded plugins are invisible to bundlers

Revere `dlopen`s several kinds of modules at runtime:

- **GStreamer element plugins** (`rtspsrc`, `rtp*`, `decodebin`, `libav`
  decoders) — required for RTSP camera streaming.
- **Motion plugins** (yolov8 → NCNN).
- **The cloud system plugin** (shipped separately, installed into the user data
  dir).

Standard dependency bundlers (`linuxdeploy`, macOS `dylibbundler`) only follow
**linked** libraries. They do **not** see `dlopen`'d plugins, so:

- The plugins themselves must be copied in manually.
- Their transitive dependencies must be gathered separately
  (`--deploy-deps-only` on Linux; a second `dylibbundler` pass on macOS).
- Each plugin's own load paths must be rewritten to point at the bundled libs.

This is the single most error-prone area of the whole build. On macOS it took
many iterations (read-only Homebrew dylibs blocking rewrites, the bundler
hanging on an interactive prompt for a missing dep, the plugins' own install-id
showing up as a false-positive `/opt` reference, etc.).

## 4. Three OSes, three completely different packaging models

There is essentially no shared packaging code across platforms:

- **Windows:** Inno Setup `.exe` installers; deps are DLLs copied next to the
  exe; dependency resolution is "DLL in the same directory."
- **Linux:** AppImage (single self-mounting file); FHS layout; deps resolved via
  `$ORIGIN` rpath; GStreamer plugins bundled via `linuxdeploy-plugin-gstreamer`;
  the snap is a separate, store-built path.
- **macOS:** `.app` bundles in a `.dmg`; deps in `Contents/libs` resolved via
  `@executable_path`; **mandatory code signing** on Apple Silicon; bundling done
  by `dylibbundler`.

Each model has its own rules for where files go, how libraries are found at
runtime, and what "valid" means. A fix on one platform usually does not transfer
to another.

## 5. macOS adds mandatory code signing on top of everything

On Apple Silicon, **any** Mach-O must carry a valid signature to run at all.
Crucially, **modifying a binary invalidates its signature** — and bundling deps
*is* modifying binaries (rewriting load paths with `install_name_tool`). So every
dylib, plugin, and app bundle we touch must be **re-signed afterward**, or macOS
refuses to launch it ("app is damaged and can't be opened").

We currently use **ad-hoc signing** (`codesign --sign -`), which makes the app
*runnable* but not Gatekeeper-clean: users still clear quarantine /
right-click→Open on first launch. Proper Developer ID signing + notarization is
feasible in CI but requires a paid Apple Developer account and is deferred.

## 6. Two apps + a version-locked, separately-built cloud plugin

A release is not one binary. It is:

- **revere** (the recorder/service) and **vision** (the viewer) — two separate
  executables/bundles that each need the full dependency + plugin treatment.
  (On macOS they are separate `.app` bundles and cannot share a plugin folder,
  so each needs its own copy of the GStreamer plugins.)
- **revere_cloud** — an optional cloud plugin built from a **separate private
  repo**, linked against revere's internal libraries, shipped as a separate
  installer (`.exe` / `.run` / `.command`).

Because the cloud plugin links revere's own libs, it must be built against the
**same** revere build (ABI lock-step). CI checks out `revere_cloud` at the
**same `vX.Y.Z` tag** and fails loudly if that tag is missing — so a release
requires tagging **both** repos.

## 7. The in-app download URL couples the binary to the release name

The app's "Download Revere Cloud" button builds a URL from the version baked
into the binary at build time:

```
github.com/dicroce/revere/releases/download/vX.Y.Z/revere-vX.Y.Z-<platform>-cloud-<ext>
```

`REVERE_VERSION` only resolves to a clean `X.Y.Z` when HEAD is **exactly on a
`v*` tag** (otherwise it falls back to the git short hash). This means:

- Release builds **must** come from a tag, or the in-app download links are dead.
- The installer filenames CI produces must match this contract exactly, so the
  release jobs include a version guard that fails if the names don't line up.

## 8. Private repo + cross-repo coordination

`revere_cloud` is private, so the public `revere` workflow can't read it with the
default token — it needs a dedicated PAT secret (`REVERE_CLOUD_TOKEN`). And all
three platform workflows publish to **one shared GitHub Release** per tag, so
their release jobs must tolerate the create-release race between them.

---

## Summary

Building Revere in CI is complicated because it is simultaneously: a **native**
app (no managed-runtime portability), with **large platform-specific multimedia
deps** (no uniform install), that **loads plugins at runtime** (invisible to
bundlers), shipped as **two apps + a separately-built version-locked cloud
plugin**, across **three OSes with totally different packaging + signing rules**,
where the binary is **coupled to the release tag name**, and the only honest test
of correctness is running the packaged artifact on a **clean machine**.

None of these is individually exotic. The difficulty is that they **stack**, and
they only reveal themselves at the distributable stage — which is exactly why so
much of the work was iterative discovery rather than up-front design.
