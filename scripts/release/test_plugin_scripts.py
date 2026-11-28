#!/usr/bin/env python3
"""Tests for write_plugin_manifest.py and write_plugin_catalog.py: python3 -m unittest scripts/release/test_plugin_scripts.py"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "write_plugin_manifest.py")
CATALOG = os.path.join(HERE, "write_plugin_catalog.py")


def run(script, *args):
    return subprocess.run([sys.executable, script, *args], capture_output=True, text=True)


class PluginScripts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.build = os.path.join(self.tmp.name, "build")
        self.out = os.path.join(self.tmp.name, "out")
        os.makedirs(self.build)

    def plugin(self, name, asset, platform="linux", arch="x86_64", asset_platform="linux", version="0.0.41"):
        path = os.path.join(self.build, name)
        with open(path, "wb") as f:
            f.write(b"plugin " + name.encode())
        r = run(MANIFEST, "--plugin", path, "--version", version, "--platform", platform, "--arch", arch,
                "--asset-platform", asset_platform, "--asset", asset, "--out", self.out)
        self.assertEqual(r.returncode, 0, r.stderr)

    def catalog(self, *extra, platform="linux", arch="x86_64", asset_platform="linux", version="0.0.41"):
        return run(CATALOG, "--dir", self.out, "--version", version, "--platform", platform, "--arch", arch,
                   "--asset-platform", asset_platform, *extra)

    def read(self, asset_platform="linux", version="0.0.41"):
        with open(os.path.join(self.out, f"revere-v{version}-{asset_platform}-plugins.json"), "rb") as f:
            raw = f.read()
        return raw, json.loads(raw)

    def test_manifest_asset_name(self):
        self.plugin("revere_cloud_system_plugin.so", "cloud")
        self.plugin("revere_homekit_system_plugin.so", "homekit")
        self.assertEqual(sorted(os.listdir(self.out)), [
            "revere-v0.0.41-linux-cloud-plugin.so", "revere-v0.0.41-linux-cloud-plugin.so.json",
            "revere-v0.0.41-linux-homekit-plugin.so", "revere-v0.0.41-linux-homekit-plugin.so.json"])
        r = run(MANIFEST, "--plugin", os.path.join(self.build, "revere_cloud_system_plugin.so"), "--version", "0.0.41",
                "--platform", "linux", "--arch", "x86_64", "--asset-platform", "linux", "--asset", "../x", "--out", self.out)
        self.assertNotEqual(r.returncode, 0)

    def test_catalog_lists_what_the_release_ships(self):
        self.plugin("revere_cloud_system_plugin.so", "cloud")
        r = self.catalog()
        self.assertEqual(r.returncode, 0, r.stderr)
        raw, c = self.read()
        # Compact, no trailing newline: these bytes are what's signed.
        self.assertEqual(raw, json.dumps(c, separators=(",", ":")).encode())
        self.assertEqual((c["format"], c["platform"], c["arch"], c["revere_version"]), (1, "linux", "x86_64", "0.0.41"))
        # Apple Home isn't in the release, so it isn't offered.
        self.assertEqual([e["id"] for e in c["plugins"]], ["revere_cloud"])
        e = c["plugins"][0]
        self.assertEqual(e["type"], "plugin")
        self.assertEqual(e["source"], "closed")
        self.assertEqual(e["terms_version"], 1)
        self.assertEqual(e["file"], "revere_cloud_system_plugin.so")
        self.assertEqual(e["plugin"], "revere-v0.0.41-linux-cloud-plugin.so")
        self.assertEqual(e["manifest"], "revere-v0.0.41-linux-cloud-plugin.so.json")
        self.assertEqual(e["signature"], "revere-v0.0.41-linux-cloud-plugin.so.json.sig")

    def test_catalog_lists_homekit_once_it_ships(self):
        self.plugin("revere_cloud_system_plugin.dylib", "cloud", "macos", "arm64", "macos")
        self.plugin("revere_homekit_system_plugin.dylib", "homekit", "macos", "arm64", "macos")
        self.assertEqual(self.catalog(platform="macos", arch="arm64", asset_platform="macos").returncode, 0)
        _, c = self.read("macos")
        self.assertEqual([e["id"] for e in c["plugins"]], ["revere_cloud", "revere_homekit"])
        self.assertTrue(c["plugins"][1]["beta"])
        self.assertNotIn("beta", c["plugins"][0])

    def test_catalog_per_platform_names(self):
        self.plugin("revere_cloud_system_plugin.so", "cloud", "linux", "arm64", "linux-arm64")
        self.assertEqual(self.catalog(arch="arm64", asset_platform="linux-arm64").returncode, 0)
        _, c = self.read("linux-arm64")
        self.assertEqual(c["plugins"][0]["plugin"], "revere-v0.0.41-linux-arm64-cloud-plugin.so")

    def test_catalog_with_no_plugins_is_still_a_catalog(self):
        os.makedirs(self.out)
        self.assertEqual(self.catalog().returncode, 0)
        self.assertEqual(self.read()[1]["plugins"], [])

    def test_catalog_refuses_a_manifest_for_another_build(self):
        self.plugin("revere_cloud_system_plugin.so", "cloud", version="0.0.40")
        os.rename(os.path.join(self.out, "revere-v0.0.40-linux-cloud-plugin.so"), os.path.join(self.out, "revere-v0.0.41-linux-cloud-plugin.so"))
        os.rename(os.path.join(self.out, "revere-v0.0.40-linux-cloud-plugin.so.json"), os.path.join(self.out, "revere-v0.0.41-linux-cloud-plugin.so.json"))
        r = self.catalog()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("revere_version", r.stderr)

    def test_catalog_refuses_another_file_name(self):
        self.plugin("renamed_system_plugin.so", "cloud")
        r = self.catalog()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("installs as", r.stderr)

    def test_bad_entries(self):
        os.makedirs(self.out)
        cases = [
            {"id": "Bad Id", "type": "link", "name": "x", "description": "x", "url": "https://example.test"},
            {"id": "a", "type": "link", "name": "x", "description": "x", "url": "http://example.test"},
            {"id": "a", "type": "theme", "name": "x", "description": "x"},
            {"id": "a", "type": "link", "name": "x", "description": "x", "url": "https://example.test", "terms_url": "https://example.test", "terms_version": 0},
            {"id": "a", "type": "plugin", "name": "x", "description": "x", "asset": "../x"},
        ]
        for entry in cases:
            path = os.path.join(self.tmp.name, "entries.json")
            json.dump({"format": 1, "plugins": [entry]}, open(path, "w"))
            r = self.catalog("--entries", path)
            self.assertNotEqual(r.returncode, 0, entry)

    def test_link_entries_are_always_listed(self):
        os.makedirs(self.out)
        path = os.path.join(self.tmp.name, "entries.json")
        json.dump({"format": 1, "plugins": [{"id": "site", "type": "link", "name": "Site", "description": "d", "url": "https://example.test/x"}]}, open(path, "w"))
        self.assertEqual(self.catalog("--entries", path).returncode, 0)
        self.assertEqual(self.read()[1]["plugins"][0]["url"], "https://example.test/x")


if __name__ == "__main__":
    unittest.main()
