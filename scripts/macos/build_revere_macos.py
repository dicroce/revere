#!/usr/bin/env python3
"""
Revere macOS Build Automation Script

This script automates the build process for Revere on macOS:
1. Configures CMake with external plugin repos
2. Builds the project
3. Creates the .dmg package
4. Creates the cloud plugin installer
5. Copies outputs to the releases directory with proper naming
"""

import os
import sys
import subprocess
import shutil
import glob
from pathlib import Path


# Configuration
# Derive REVERE_DIR from the script's own location (scripts/macos/ -> repo root)
SCRIPT_DIR = Path(__file__).resolve().parent
REVERE_DIR = SCRIPT_DIR.parent.parent
EXTERNAL_PLUGIN_REPOS = os.environ.get("REVERE_CLOUD_DIR", str(REVERE_DIR.parent / "revere_cloud"))
BUILD_DIR = REVERE_DIR / "build"
RELEASES_DIR = Path(os.environ.get("REVERE_RELEASES_DIR", str(REVERE_DIR.parent / "revere_releases")))
SCRIPTS_DIR = REVERE_DIR / "scripts"


def run_command(cmd, cwd=None, shell=False):
    """Run a command and stream output to console."""
    print(f"\n{'='*80}")
    print(f"Running: {cmd if isinstance(cmd, str) else ' '.join(cmd)}")
    print(f"{'='*80}\n")

    result = subprocess.run(
        cmd,
        cwd=cwd,
        shell=shell,
        check=True,
        text=True
    )
    return result


def get_git_version():
    """Get the version from git: an exact tag if on one, otherwise the short hash."""
    # Try exact tag match first
    result = subprocess.run(
        ["git", "describe", "--tags", "--exact-match"],
        cwd=REVERE_DIR,
        capture_output=True,
        text=True,
        check=False
    )
    if result.returncode == 0:
        version = result.stdout.strip()
        print(f"Git version (tag): {version}")
        return version

    # Fall back to short hash
    result = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=REVERE_DIR,
        capture_output=True,
        text=True,
        check=True
    )
    version = result.stdout.strip()
    print(f"Git version (hash): {version}")
    return version


def clean_build_dir():
    """Clean and create the build directory."""
    if BUILD_DIR.exists():
        print(f"Cleaning build directory: {BUILD_DIR}")
        shutil.rmtree(BUILD_DIR)

    print(f"Creating build directory: {BUILD_DIR}")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)


def configure_cmake():
    """Run CMake configuration."""
    cmake_cmd = [
        "cmake",
        f"-DEXTERNAL_PLUGIN_REPOS={EXTERNAL_PLUGIN_REPOS}",
        "-DCMAKE_BUILD_TYPE=Release",
        ".."
    ]
    run_command(cmake_cmd, cwd=BUILD_DIR)


def build_project():
    """Build the project with make."""
    run_command(["make"], cwd=BUILD_DIR)


def create_package():
    """Create the .dmg package."""
    run_command(["make", "package"], cwd=BUILD_DIR)


def create_cloud_plugin_installer(version):
    """Create a self-extracting .command installer for the cloud plugin.

    The resulting file is a bash script with the .dylib binary appended after
    a __PAYLOAD__ marker. Users double-click the .command file to install.
    """
    plugin_path = BUILD_DIR / "_deps" / "revere_cloud-build" / "revere_cloud_system_plugin.dylib"
    output_file = BUILD_DIR / f"install-revere-cloud-plugin-{version}-macos.command"

    if not plugin_path.exists():
        print(f"ERROR: Plugin not found at {plugin_path}")
        print("Build revere with -DEXTERNAL_PLUGIN_REPOS=/path/to/revere_cloud first")
        sys.exit(1)

    print(f"\nCreating cloud plugin installer for version: {version}")
    print(f"  Plugin: {plugin_path}")
    print(f"  Output: {output_file}")

    installer_header = """\
#!/bin/bash
# Revere Cloud Plugin Installer for macOS
# This is a self-extracting installer - the .dylib is appended after the __PAYLOAD__ marker

set -e

PLUGIN_DIR="$HOME/Library/Application Support/Revere/revere/system_plugins"
PLUGIN_NAME="revere_cloud_system_plugin.dylib"

echo "Revere Cloud Plugin Installer"
echo "=============================="
echo ""

# Check if revere directory exists
if [ ! -d "$HOME/Library/Application Support/Revere/revere" ]; then
    echo "Error: Revere does not appear to be installed."
    echo "Please run Revere at least once before installing the plugin."
    echo ""
    read -p "Press Enter to exit..."
    exit 1
fi

# Create plugin directory if needed
mkdir -p "$PLUGIN_DIR"

# Extract payload (everything after __PAYLOAD__ marker)
ARCHIVE_START=$(awk '/^__PAYLOAD__$/{print NR + 1; exit 0; }' "$0")

echo "Installing plugin to: $PLUGIN_DIR/$PLUGIN_NAME"
tail -n +"$ARCHIVE_START" "$0" > "$PLUGIN_DIR/$PLUGIN_NAME"
chmod 755 "$PLUGIN_DIR/$PLUGIN_NAME"

echo ""
echo "Installation complete!"
echo "Restart Revere to load the cloud plugin."
echo ""
read -p "Press Enter to exit..."

exit 0

__PAYLOAD__
"""

    # Write the header then append the binary plugin
    with open(output_file, "wb") as f:
        f.write(installer_header.encode("utf-8"))
        f.write(plugin_path.read_bytes())

    # Make executable
    output_file.chmod(0o755)

    size_mb = output_file.stat().st_size / (1024 * 1024)
    print(f"  Installer created ({size_mb:.1f} MB)")


def find_and_copy_outputs(version):
    """Find the build outputs and copy them to the releases directory with proper naming."""
    # Ensure releases directory exists
    RELEASES_DIR.mkdir(parents=True, exist_ok=True)

    # Find the .dmg file
    dmg_pattern = str(BUILD_DIR / "Revere-*.dmg")
    dmg_files = glob.glob(dmg_pattern)

    if not dmg_files:
        print(f"ERROR: No .dmg file found matching pattern: {dmg_pattern}")
        sys.exit(1)

    if len(dmg_files) > 1:
        print(f"WARNING: Multiple .dmg files found, using first one: {dmg_files[0]}")

    dmg_source = Path(dmg_files[0])

    # Find the .command file
    command_pattern = str(BUILD_DIR / "install-revere-cloud-plugin-*.command")
    command_files = glob.glob(command_pattern)

    if not command_files:
        print(f"ERROR: No .command file found matching pattern: {command_pattern}")
        sys.exit(1)

    if len(command_files) > 1:
        print(f"WARNING: Multiple .command files found, using first one: {command_files[0]}")

    command_source = Path(command_files[0])

    # Create destination filenames with proper format
    # Format: revere-v0.0.1-x86_64-macos.dmg and revere_cloud-v0.0.1-x86_64-macos.command
    # If version already starts with 'v', use as-is; otherwise prepend 'v'
    version_tag = version if version.startswith('v') else f'v{version}'

    dmg_dest = RELEASES_DIR / f"revere-{version_tag}-macos-app.dmg"
    command_dest = RELEASES_DIR / f"revere-{version_tag}-macos-cloud.command"

    # Copy files
    print(f"\nCopying outputs to releases directory:")
    print(f"  {dmg_source} -> {dmg_dest}")
    shutil.copy2(dmg_source, dmg_dest)

    print(f"  {command_source} -> {command_dest}")
    shutil.copy2(command_source, command_dest)

    print(f"\n{'='*80}")
    print("Build Complete!")
    print(f"{'='*80}")
    print(f"\nRelease files created in {RELEASES_DIR}:")
    print(f"  - {dmg_dest.name}")
    print(f"  - {command_dest.name}")
    print()


def main():
    """Main build process."""
    try:
        # Verify directories exist
        if not REVERE_DIR.exists():
            print(f"ERROR: Revere directory not found: {REVERE_DIR}")
            sys.exit(1)

        if not Path(EXTERNAL_PLUGIN_REPOS).exists():
            print(f"ERROR: External plugin repos directory not found: {EXTERNAL_PLUGIN_REPOS}")
            sys.exit(1)

        # Get version first
        version = get_git_version()

        # Build process
        clean_build_dir()
        configure_cmake()
        build_project()
        create_package()
        create_cloud_plugin_installer(version)
        find_and_copy_outputs(version)

    except subprocess.CalledProcessError as e:
        print(f"\nERROR: Command failed with exit code {e.returncode}")
        sys.exit(1)
    except Exception as e:
        print(f"\nERROR: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
