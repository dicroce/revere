#!/usr/bin/env python3
"""Copies a built system plugin out as a release asset and writes its manifest.

The release workflows run this in their build jobs, after any step that changes
the plugin's bytes (on macOS: install_name_tool and the ad-hoc codesign), so the
manifest describes exactly the file users install. The workflow's sign job then
signs the manifest (project-planning docs/IN_APP_PLUGIN_INSTALL.md, section 2a),
and revere checks both before loading the plugin (libs/r_vss/r_plugin_manifest.h).

    write_plugin_manifest.py --plugin build/system_plugins/revere_cloud_system_plugin.so \
        --version 0.0.41 --platform linux --arch x86_64 --asset-platform linux \
        --out dist

writes dist/revere-v0.0.41-linux-cloud-plugin.so and its manifest,
dist/revere-v0.0.41-linux-cloud-plugin.so.json:

    {"format":1,"platform":"linux","arch":"x86_64","revere_version":"0.0.41",
     "file":"revere_cloud_system_plugin.so","size":...,"sha256":"..."}

"file" is the name the plugin is installed under (the built file's name), not
the asset's: revere checks it against the file it's about to load.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys

PLATFORMS = {"windows": ".dll", "macos": ".dylib", "linux": ".so"}
ARCHES = {"x86_64", "arm64"}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--plugin", required=True, help="the built plugin file")
    p.add_argument("--version", required=True, help="the revere version it's for, as revere_version() reports it (no leading v)")
    p.add_argument("--platform", required=True, choices=sorted(PLATFORMS))
    p.add_argument("--arch", required=True, choices=sorted(ARCHES))
    p.add_argument("--asset-platform", required=True,
                   help="the platform part of the asset names: windows, macos, macos-intel, linux, linux-arm64")
    p.add_argument("--out", required=True, help="folder to write the asset and manifest into")
    a = p.parse_args()

    version = a.version[1:] if a.version.startswith("v") else a.version
    if not version or re.search(r"[^0-9A-Za-z.\-]", version):
        sys.exit(f"error: unexpected version {a.version!r}")
    if not re.fullmatch(r"[a-z0-9\-]+", a.asset_platform):
        sys.exit(f"error: unexpected asset platform {a.asset_platform!r}")

    ext = PLATFORMS[a.platform]
    file_name = os.path.basename(a.plugin)
    if not file_name.endswith(ext):
        sys.exit(f"error: {file_name} isn't a {ext} file, as a {a.platform} plugin must be")

    data = open(a.plugin, "rb").read()
    manifest = {
        "format": 1,
        "platform": a.platform,
        "arch": a.arch,
        "revere_version": version,
        "file": file_name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }

    os.makedirs(a.out, exist_ok=True)
    asset = os.path.join(a.out, f"revere-v{version}-{a.asset_platform}-cloud-plugin{ext}")
    shutil.copyfile(a.plugin, asset)
    # Compact and with no trailing newline: these exact bytes are what's signed.
    with open(asset + ".json", "wb") as f:
        f.write(json.dumps(manifest, separators=(",", ":")).encode("utf-8"))

    print(f"{asset}: {manifest['size']} bytes, sha256 {manifest['sha256']}")
    print(f"{asset}.json: {json.dumps(manifest)}")


if __name__ == "__main__":
    main()
