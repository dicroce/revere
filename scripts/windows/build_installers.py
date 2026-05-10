#!/usr/bin/env python3
"""
Automated build script for Revere and RevereCloud Windows installers.
Must be run from x64 Native Tools Command Prompt for Visual Studio with Administrator privileges.

Usage:
    python build_installers.py [options]

Options:
    --skip-build        Skip the CMake build step (use existing build)
    --skip-install      Skip the CMake install step
    --skip-inno         Skip Inno Setup compilation
    --clean             Clean build directory before building
    -h, --help          Show this help message
"""

import subprocess
import sys
import os
import re
import shutil
from pathlib import Path
import argparse


# Configuration
REVERE_DIR = Path(r"c:\dev\revere")
REVERE_CLOUD_DIR = Path(r"c:\dev\revere_cloud")
BUILD_DIR = REVERE_DIR / "build"
INSTALL_DIR = Path(r"C:\Program Files (x86)\revere")
INNO_COMPILER = Path(r"C:\Program Files (x86)\Inno Setup 6\iscc.exe")
OUTPUT_DIR = Path(r"C:\dev\revere_releases")


def run_command(cmd, cwd=None, description=None):
    """Run a command and stream output in real-time.

    Args:
        cmd: Command to run (list or string)
        cwd: Working directory
        description: Description to print
    """
    if description:
        print(f"\n{'='*60}")
        print(f"  {description}")
        print(f"{'='*60}")

    print(f"Running: {' '.join(cmd) if isinstance(cmd, list) else cmd}\n")
    sys.stdout.flush()

    try:
        # Stream output in real-time so user can see progress
        result = subprocess.run(
            cmd,
            cwd=cwd,
            check=True,
            shell=True if isinstance(cmd, str) else False
        )
        return result
    except subprocess.CalledProcessError as e:
        print(f"\nERROR: Command failed with exit code {e.returncode}")
        sys.exit(1)


def get_git_version():
    """Extract version from git tags in revere repository."""
    try:
        # Try to get exact tag match
        result = subprocess.run(
            ["git", "describe", "--tags", "--exact-match", "--match", "v[0-9]*"],
            cwd=REVERE_DIR,
            capture_output=True,
            text=True,
            check=False
        )

        if result.returncode == 0:
            # We're on a tag, extract version
            tag = result.stdout.strip()
            match = re.match(r"^v([0-9]+\.[0-9]+\.[0-9]+)", tag)
            if match:
                return match.group(1)

        # Fall back to short hash
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=REVERE_DIR,
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()

    except Exception as e:
        print(f"Warning: Could not determine git version: {e}")
        return "unknown"


def clean_build_directory():
    """Remove the build directory and installer outputs to start fresh."""
    if BUILD_DIR.exists():
        print(f"\nCleaning build directory: {BUILD_DIR}")

        # Windows requires handling read-only files in git repos
        def handle_remove_readonly(func, path, exc):
            """Error handler for Windows readonly files."""
            import stat
            if not os.access(path, os.W_OK):
                # Change the file to be writable
                os.chmod(path, stat.S_IWUSR | stat.S_IREAD)
                # Try the operation again
                func(path)
            else:
                raise

        shutil.rmtree(BUILD_DIR, onerror=handle_remove_readonly)
        print("Build directory cleaned.")

    # Clean up installer outputs
    print(f"\nCleaning installer outputs in: {OUTPUT_DIR}")
    installer_patterns = [
        "revere-v*-windows-app-setup.exe",
        "revere-v*-windows-cloud-setup.exe",
        "RevereSetup.exe",
        "RevereCloudSetup.exe"
    ]

    deleted_count = 0
    for pattern in installer_patterns:
        for installer in OUTPUT_DIR.glob(pattern):
            print(f"  Deleting: {installer.name}")
            installer.unlink()
            deleted_count += 1

    if deleted_count > 0:
        print(f"Deleted {deleted_count} installer file(s).")
    else:
        print("No installer files found to delete.")


def configure_cmake():
    """Configure CMake with external plugin."""
    BUILD_DIR.mkdir(exist_ok=True)

    cmd = [
        "cmake",
        f"-DCMAKE_INSTALL_PREFIX={INSTALL_DIR.as_posix()}",
        f"-DEXTERNAL_PLUGIN_REPOS={REVERE_CLOUD_DIR.as_posix()}",
        ".."
    ]

    run_command(cmd, cwd=BUILD_DIR, description="Configuring CMake")


def build_release():
    """Build the project in Release configuration."""
    cmd = ["cmake", "--build", ".", "--config", "Release"]
    run_command(cmd, cwd=BUILD_DIR, description="Building Release configuration")


def install_binaries():
    """Install binaries to Program Files."""
    cmd = ["cmake", "--build", ".", "--target", "install", "--config", "Release"]
    run_command(cmd, cwd=BUILD_DIR, description="Installing binaries to Program Files")


def compile_inno_setup(iss_file, project_dir, project_name):
    """Compile Inno Setup script."""
    iss_path = project_dir / iss_file

    if not iss_path.exists():
        print(f"ERROR: Inno Setup script not found: {iss_path}")
        sys.exit(1)

    if not INNO_COMPILER.exists():
        print(f"ERROR: Inno Setup compiler not found: {INNO_COMPILER}")
        sys.exit(1)

    # Create output directory if it doesn't exist
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Use /O to override OutputDir in the .iss file
    cmd = [str(INNO_COMPILER), f'/O{OUTPUT_DIR}', str(iss_path)]
    run_command(cmd, description=f"Compiling {project_name} Inno Setup installer")


def rename_installer(old_name, new_name_template, version):
    """Rename installer with version number."""
    old_path = OUTPUT_DIR / old_name
    new_name = new_name_template.format(version=version)
    new_path = OUTPUT_DIR / new_name

    if not old_path.exists():
        print(f"WARNING: Installer not found: {old_path}")
        return

    # Remove existing file if it exists
    if new_path.exists():
        print(f"Removing existing file: {new_path}")
        new_path.unlink()

    print(f"Renaming: {old_path.name} -> {new_path.name}")
    old_path.rename(new_path)
    print(f"Created: {new_path}")


def check_prerequisites():
    """Check that required tools and directories exist."""
    errors = []

    if not REVERE_DIR.exists():
        errors.append(f"Revere directory not found: {REVERE_DIR}")

    if not REVERE_CLOUD_DIR.exists():
        errors.append(f"Revere Cloud directory not found: {REVERE_CLOUD_DIR}")

    if not INNO_COMPILER.exists():
        errors.append(f"Inno Setup compiler not found: {INNO_COMPILER}")

    # Check if running from x64 Native Tools Command Prompt
    if "VSCMD_ARG_TGT_ARCH" not in os.environ or os.environ.get("VSCMD_ARG_TGT_ARCH") != "x64":
        errors.append(
            "This script must be run from 'x64 Native Tools Command Prompt for VS'\n"
            "Please start the x64 Native Tools Command Prompt with Administrator privileges."
        )

    if errors:
        print("ERROR: Prerequisites not met:\n")
        for error in errors:
            print(f"  - {error}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Build Revere and RevereCloud Windows installers",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("--skip-build", action="store_true", help="Skip CMake build step")
    parser.add_argument("--skip-install", action="store_true", help="Skip CMake install step")
    parser.add_argument("--skip-inno", action="store_true", help="Skip Inno Setup compilation")
    parser.add_argument("--clean", action="store_true", help="Clean build directory before building")

    args = parser.parse_args()

    print("="*60)
    print("  Revere Windows Installer Build Script")
    print("="*60)

    # Check prerequisites
    check_prerequisites()

    # Get version from git
    version = get_git_version()
    print(f"\nDetected version: {version}")

    # Clean if requested
    if args.clean:
        clean_build_directory()

    # Build steps
    if not args.skip_build:
        configure_cmake()
        build_release()
    else:
        print("\nSkipping build step (--skip-build)")

    # Install step
    if not args.skip_install:
        install_binaries()
    else:
        print("\nSkipping install step (--skip-install)")

    # Inno Setup compilation
    if not args.skip_inno:
        compile_inno_setup("RevereSetup.iss", REVERE_DIR, "Revere")
        compile_inno_setup("RevereCloudSetup.iss", REVERE_CLOUD_DIR, "RevereCloud")

        # Rename installers with version
        rename_installer(
            "RevereSetup.exe",
            "revere-v{version}-windows-app-setup.exe",
            version
        )
        rename_installer(
            "RevereCloudSetup.exe",
            "revere-v{version}-windows-cloud-setup.exe",
            version
        )
    else:
        print("\nSkipping Inno Setup compilation (--skip-inno)")

    print("\n" + "="*60)
    print("  Build completed successfully!")
    print("="*60)
    print(f"\nInstallers created in: {OUTPUT_DIR}")
    print(f"  - revere-v{version}-windows-app-setup.exe")
    print(f"  - revere-v{version}-windows-cloud-setup.exe")


if __name__ == "__main__":
    main()
