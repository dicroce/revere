#!/usr/bin/env python3
"""Writes a release's plugin catalog for one platform (libs/r_vss/r_plugin_catalog.h).

The release workflows run this in their build jobs, after write_plugin_manifest.py
has written each plugin's asset and manifest into one folder. The workflow's sign
job then signs the catalog like a manifest, and revere checks it with the same keys
before the Store page lists anything.

    write_plugin_catalog.py --dir cloud --version 0.0.41 --platform linux \
        --arch x86_64 --asset-platform linux

reads scripts/release/plugin_catalog_entries.json and writes
cloud/revere-v0.0.41-linux-plugins.json. An entry of type "plugin" names the asset
its plugin was written as (--asset in write_plugin_manifest.py: "cloud" is
revere-v0.0.41-linux-cloud-plugin.so) and is listed only if that asset and its
manifest are in --dir, so a plugin the release doesn't ship for this platform
isn't offered; the manifest decides the file name it installs as. An entry of type
"link" is always listed. The bytes written are compact and have no trailing newline,
because these exact bytes are what's signed.
"""

import argparse
import json
import os
import re
import sys

PLATFORMS = {"windows": ".dll", "macos": ".dylib", "linux": ".so"}
ARCHES = {"x86_64", "arm64"}
ENTRIES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "plugin_catalog_entries.json")
ID = re.compile(r"[a-z0-9_]{1,40}")
ASSET = re.compile(r"[a-z0-9]+")


def fail(message):
    sys.exit(f"error: {message}")


def https(url):
    return isinstance(url, str) and url.startswith("https://") and len(url) > 8 and not re.search(r"\s", url)


def list_entry(entry, platform, arch, version, asset_platform, directory):
    """The catalog entry for this platform, or None if the release doesn't ship it here."""
    ident = entry.get("id")
    if not isinstance(ident, str) or not ID.fullmatch(ident):
        fail(f"entry id {ident!r} isn't 1 to 40 of a-z, 0-9 and _")
    for key in ("name", "description"):
        if not isinstance(entry.get(key), str) or not entry[key]:
            fail(f"entry {ident} has no {key}")
    kind = entry.get("type")
    out = {"id": ident, "type": kind, "name": entry["name"], "description": entry["description"],
           "source": entry.get("source", "open")}
    if out["source"] not in ("open", "closed"):
        fail(f"entry {ident}: source must be open or closed")
    if entry.get("beta"):
        out["beta"] = True
    if "terms_version" in entry or "terms_url" in entry:
        if not https(entry.get("terms_url")):
            fail(f"entry {ident}: terms_url isn't an https url")
        version_number = entry.get("terms_version")
        if not isinstance(version_number, int) or isinstance(version_number, bool) or version_number < 1:
            fail(f"entry {ident}: terms_version isn't a whole number from 1")
        out["terms_url"] = entry["terms_url"]
        out["terms_version"] = version_number

    if kind == "link":
        if not https(entry.get("url")):
            fail(f"entry {ident}: url isn't an https url")
        out["url"] = entry["url"]
        return out
    if kind != "plugin":
        fail(f"entry {ident}: type must be plugin or link")

    asset = entry.get("asset")
    if not isinstance(asset, str) or not ASSET.fullmatch(asset):
        fail(f"entry {ident}: asset isn't a plain name")
    plugin = f"revere-v{version}-{asset_platform}-{asset}-plugin{PLATFORMS[platform]}"
    manifest_path = os.path.join(directory, plugin + ".json")
    if not (os.path.isfile(os.path.join(directory, plugin)) and os.path.isfile(manifest_path)):
        return None
    manifest = json.load(open(manifest_path))
    # The manifest is what revere checks the plugin against; the catalog must agree with it.
    for key, want in (("platform", platform), ("arch", arch), ("revere_version", version)):
        if manifest.get(key) != want:
            fail(f"{plugin}.json has {key} {manifest.get(key)!r}, not {want!r}")
    file_name = manifest.get("file")
    if not isinstance(file_name, str) or not file_name.endswith(PLATFORMS[platform]):
        fail(f"{plugin}.json names no {PLATFORMS[platform]} file")
    if entry.get("file") and file_name != entry["file"] + PLATFORMS[platform]:
        fail(f"{plugin}.json installs as {file_name}, the entry expects {entry['file'] + PLATFORMS[platform]}")
    out.update({"file": file_name, "plugin": plugin, "manifest": plugin + ".json", "signature": plugin + ".json.sig"})
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dir", required=True, help="the folder write_plugin_manifest.py wrote the plugins into; the catalog goes here too")
    p.add_argument("--version", required=True, help="the revere version, as revere_version() reports it (no leading v)")
    p.add_argument("--platform", required=True, choices=sorted(PLATFORMS))
    p.add_argument("--arch", required=True, choices=sorted(ARCHES))
    p.add_argument("--asset-platform", required=True,
                   help="the platform part of the asset names: windows, macos, macos-intel, linux, linux-arm64")
    p.add_argument("--entries", default=ENTRIES, help="the entries file (default: plugin_catalog_entries.json beside this script)")
    a = p.parse_args()

    version = a.version[1:] if a.version.startswith("v") else a.version
    if not version or re.search(r"[^0-9A-Za-z.\-]", version):
        fail(f"unexpected version {a.version!r}")
    if not re.fullmatch(r"[a-z0-9\-]+", a.asset_platform):
        fail(f"unexpected asset platform {a.asset_platform!r}")

    source = json.load(open(a.entries))
    if source.get("format") != 1 or not isinstance(source.get("plugins"), list):
        fail(f"{a.entries} isn't a format 1 entries file")

    listed = []
    for entry in source["plugins"]:
        item = list_entry(entry, a.platform, a.arch, version, a.asset_platform, a.dir)
        if item is not None:
            if any(i["id"] == item["id"] for i in listed):
                fail(f"entry {item['id']} is there twice")
            listed.append(item)

    catalog = {"format": 1, "platform": a.platform, "arch": a.arch, "revere_version": version, "plugins": listed}
    path = os.path.join(a.dir, f"revere-v{version}-{a.asset_platform}-plugins.json")
    with open(path, "wb") as f:
        f.write(json.dumps(catalog, separators=(",", ":")).encode("utf-8"))
    print(f"{path}: {[i['id'] for i in listed]}")


if __name__ == "__main__":
    main()
