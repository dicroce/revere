#!/usr/bin/env python3
"""Regenerate licenses/ from scripts/licenses/components.json.

    python scripts/licenses/fetch_licenses.py           # fetch everything
    python scripts/licenses/fetch_licenses.py ffmpeg    # just these ids

For each component it writes licenses/<id>.txt: a short header (name, version,
licence, where the source is) followed by every licence text it fetched. Then
it writes licenses/README.md, the index. A component whose sources all fail
keeps its previous file, and the script exits non-zero after listing them.
licenses/opencv-windows/ is copied by hand from the OpenCV install's
etc/licenses and is left alone.
"""

import json
import pathlib
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "scripts" / "licenses" / "components.json"
OUT = ROOT / "licenses"

PLATFORM_NAMES = {"win": "Windows", "mac": "macOS", "appimage": "Linux AppImage", "deb": "Raspberry Pi .deb"}
UA = "revere-license-fetch/1.0"


def _get(url):
    for attempt in range(3):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise
            last = e
        except Exception as e:  # network hiccups
            last = e
        time.sleep(2 * (attempt + 1))
    raise last


def _debian_prefix(pkg):
    return pkg[:4] if pkg.startswith("lib") else pkg[0]


def _debian_copyright(pkg):
    """Debian's copyright file for source package pkg, newest stable-ish version."""
    info = json.loads(_get(f"https://sources.debian.org/api/src/{urllib.parse.quote(pkg)}/"))
    versions = info.get("versions") or []
    if not versions:
        raise RuntimeError(f"no Debian versions for {pkg}")
    pick = None
    for suite in ("trixie", "bookworm", "sid", "forky"):
        pick = next((v for v in versions if suite in v.get("suites", [])), None)
        if pick:
            break
    pick = pick or versions[0]
    ver = pick["version"]
    area = pick.get("area", "main")
    url = (f"https://sources.debian.org/data/{area}/{_debian_prefix(pkg)}/"
           f"{urllib.parse.quote(pkg)}/{urllib.parse.quote(ver)}/debian/copyright")
    return url, _get(url)


def _as_list(v):
    return v if isinstance(v, list) else [v]


def _fetch_one(src):
    """[(origin, text)] for one source entry, or raise."""
    parts = []
    if "text" in src:
        parts.append(("written for Revere", src["text"]))
    if "repo" in src:
        p = ROOT / src["repo"]
        parts.append((f"this repository: {src['repo']}", p.read_text(encoding="utf-8", errors="replace")))
    for pkg in _as_list(src.get("debian", [])):
        try:
            url, text = _debian_copyright(pkg)
            parts.append((f"Debian source package {pkg}: {url}", text))
        except Exception as e:
            if not src.get("all"):
                raise
            print(f"    ! debian {pkg}: {e}")
    for url in _as_list(src.get("url", [])):
        try:
            parts.append((url, _get(url)))
        except Exception as e:
            if not src.get("all"):
                raise
            print(f"    ! {url}: {e}")
    if not parts:
        raise RuntimeError("nothing fetched")
    return parts


def _fetch(component):
    errors = []
    for src in component["sources"]:
        try:
            return _fetch_one(src)
        except Exception as e:
            errors.append(str(e))
    raise RuntimeError("; ".join(errors))


def _header(c):
    lines = [
        c["name"],
        "=" * min(len(c["name"]), 78),
        "",
        f"Licence:   {c['spdx']}",
    ]
    if c.get("version"):
        lines.append(f"Version:   {c['version']}")
    lines.append(f"Shipped in: {', '.join(PLATFORM_NAMES[p] for p in c['platforms'])}")
    lines.append(f"Used for:  {c['use']}")
    lines.append(f"Source:    {c['source']}")
    return "\n".join(lines) + "\n"


def _write_component(c, parts):
    out = [_header(c)]
    for origin, text in parts:
        out.append("\n" + "-" * 78 + f"\nFrom {origin}\n" + "-" * 78 + "\n\n" + text.rstrip() + "\n")
    (OUT / f"{c['id']}.txt").write_text("".join(out), encoding="utf-8", newline="\n")


def _write_index(manifest):
    md = [
        "# Third-party licences",
        "",
        "Revere itself is BSD-3-Clause licensed (see [revere.txt](revere.txt), a copy of the repository's",
        "LICENSE). Its packages also contain the",
        "third-party software below, each under its own licence. Every component's full licence text is in",
        "the file linked from its name.",
        "",
        "**Source code for the LGPL components** (FFmpeg, GStreamer, GLib and the others marked LGPL) is",
        "available from the source links below, at the versions listed. On request we will also provide the",
        "exact source we built from; open an issue at https://github.com/dicroce/revere.",
        "",
        "Generated by `scripts/licenses/fetch_licenses.py` from `scripts/licenses/components.json`;",
        "edit the JSON, not this file.",
        "",
    ]
    for g in manifest["groups"]:
        md += [f"## {g['title']}", ""]
        if g.get("note"):
            md += [g["note"], ""]
        md += ["| Component | Version | Licence | Packages |", "|---|---|---|---|"]
        for c in g["components"]:
            plats = ", ".join(PLATFORM_NAMES[p] for p in c["platforms"])
            md.append(f"| [{c['name']}]({c['id']}.txt) | {c.get('version') or '-'} | {c['spdx']} | {plats} |")
        md.append("")
    md += [
        "## OpenCV's bundled libraries (Windows)",
        "",
        "The Windows OpenCV build compiles in libjpeg-turbo, libpng, libtiff, OpenJPEG, OpenEXR, libwebp,",
        "zlib, protobuf, Intel ITT, Intel IPP (ippicv/ippiw) and a few smaller pieces. Their licences, as",
        "OpenCV installs them, are in [opencv-windows/](opencv-windows/).",
        "",
    ]
    (OUT / "README.md").write_text("\n".join(md), encoding="utf-8", newline="\n")


def main():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    wanted = set(sys.argv[1:])
    OUT.mkdir(exist_ok=True)
    failed = []
    for g in manifest["groups"]:
        for c in g["components"]:
            if wanted and c["id"] not in wanted:
                continue
            print(f"{c['id']}")
            try:
                _write_component(c, _fetch(c))
            except Exception as e:
                print(f"    FAILED: {e}")
                failed.append(c["id"])
    # Revere's own licence, so the directory stands alone once installed.
    (OUT / "revere.txt").write_text((ROOT / "LICENSE").read_text(encoding="utf-8"), encoding="utf-8", newline="\n")
    _write_index(manifest)
    if failed:
        print("\nFailed: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
