#!/usr/bin/env python3
"""
Automated build script for Revere and RevereCloud Windows installers.
Must be run from x64 Native Tools Command Prompt for Visual Studio.

Installs to a local staging directory (build/stage by default), so no admin
rights are required. All paths are overridable via environment variables
(REVERE_DIR, REVERE_CLOUD_DIR, REVERE_INSTALL_DIR, REVERE_RELEASES_DIR,
INNO_COMPILER, VC_REDIST_PATH) for CI use.

Usage:
    python build_installers.py [options]

Options:
    --skip-configure    Skip the CMake configure step (WARNING: new/removed source files
                        will not be detected — only use when you are certain nothing changed)
    --skip-build        Skip the CMake compile step (configure still runs unless --skip-configure)
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


# DLLs provided by Windows itself or the bundled VC++ redistributable. An import
# of one of these is NOT a packaging error even though the file isn't staged.
# Anything matching is treated as "resolvable on a clean machine".
SYSTEM_DLL_RE = re.compile(
    r"^(?:"
    r"api-ms-win-.*|ext-ms-win-.*|"                      # API set stubs
    r"msvcp140.*|vcruntime140.*|concrt140|msvcrt|"       # VC++ runtime (VCRedist)
    r"kernel32|kernelbase|ntdll|user32|gdi32|gdiplus|"
    r"advapi32|shell32|shlwapi|ole32|oleaut32|combase|"
    r"rpcrt4|sechost|ws2_32|wsock32|crypt32|secur32|"
    r"bcrypt|ncrypt|dnsapi|iphlpapi|winmm|setupapi|"
    r"cfgmgr32|comctl32|comdlg32|version|userenv|psapi|"
    r"dbghelp|powrprof|wtsapi32|imm32|uxtheme|propsys|"
    r"dwmapi|d3d9|d3d11|d3d12|dxgi|d3dcompiler.*|"
    r"opengl32|glu32|d2d1|dwrite|mf|mfplat|mfreadwrite|"  # d2d1/dwrite: pulled by avcodec
    r"mfcore|avicap32|vfw32|winhttp|wininet|urlmon|"
    r"normaliz|bcryptprimitives|win32u|gdi32full|msvcp_win"
    r")\.dll$",
    re.IGNORECASE,
)


# Configuration — every path is overridable via an environment variable (for CI)
# and falls back to the historical local-dev default when the variable is unset.
REVERE_DIR = Path(os.environ.get("REVERE_DIR", r"c:\dev\revere"))
REVERE_CLOUD_DIR = Path(os.environ.get("REVERE_CLOUD_DIR", r"c:\dev\revere_cloud"))
BUILD_DIR = REVERE_DIR / "build"
# Install/staging directory. Defaults to a local staging dir inside the build tree
# so packaging needs no admin rights and never touches Program Files. CMake lays
# files under <prefix>/revere, and that subdir is what the Inno scripts consume.
INSTALL_DIR = Path(os.environ.get("REVERE_INSTALL_DIR", str(BUILD_DIR / "stage")))
STAGING_DIR = INSTALL_DIR / "revere"
INNO_COMPILER = Path(os.environ.get("INNO_COMPILER", r"C:\Program Files (x86)\Inno Setup 6\iscc.exe"))
OUTPUT_DIR = Path(os.environ.get("REVERE_RELEASES_DIR", r"C:\dev\revere_releases"))
VC_REDIST = Path(os.environ.get("VC_REDIST_PATH", r"C:\dev\VC_redist.x64.exe"))


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
    """Install binaries to the staging directory (CMAKE_INSTALL_PREFIX)."""
    cmd = ["cmake", "--build", ".", "--target", "install", "--config", "Release"]
    run_command(cmd, cwd=BUILD_DIR, description=f"Installing binaries to {INSTALL_DIR}")


def verify_dependencies():
    """Fail the build if any staged binary imports a DLL we don't ship.

    Walks every .exe/.dll under STAGING_DIR, reads its import table with
    dumpbin, and flags any imported DLL that is neither present somewhere in the
    staged tree nor a known system/VCRedist DLL. This is the guard that would
    have caught the opencv_dnn4120.dll regression: opencv_video4120.dll imports
    opencv_dnn4120.dll, which loads on dev machines (OpenCV bin is on PATH) but
    is absent on a clean install -> startup failure. A missing third-party DLL
    here is a release blocker.
    """
    print(f"\n{'='*60}")
    print("  Verifying bundled DLL dependencies")
    print(f"{'='*60}")

    if not STAGING_DIR.exists():
        print(f"WARNING: staging dir not found ({STAGING_DIR}); skipping check.")
        return

    dumpbin = shutil.which("dumpbin")
    if not dumpbin:
        # dumpbin ships with MSVC and is on PATH in the x64 Native Tools prompt
        # (and in CI after msvc-dev-cmd). If it's missing we can't verify; warn
        # loudly rather than block the build on tooling.
        print("WARNING: dumpbin not found on PATH; cannot verify dependencies.")
        return

    binaries = [p for p in STAGING_DIR.rglob("*")
                if p.suffix.lower() in (".dll", ".exe")]
    present = {p.name.lower() for p in binaries}

    dep_line = re.compile(r"^\s+(\S+\.dll)\s*$", re.IGNORECASE)
    missing = {}  # missing dll (lower) -> set of importers
    for binary in binaries:
        try:
            out = subprocess.run(
                [dumpbin, "/dependents", str(binary)],
                capture_output=True, text=True, check=True,
            ).stdout
        except subprocess.CalledProcessError as e:
            print(f"WARNING: dumpbin failed on {binary.name}: {e}")
            continue
        for line in out.splitlines():
            m = dep_line.match(line)
            if not m:
                continue
            dep = m.group(1)
            dep_l = dep.lower()
            if dep_l in present or SYSTEM_DLL_RE.match(dep):
                continue
            missing.setdefault(dep_l, set()).add(binary.name)

    if missing:
        print("\nERROR: staged binaries import DLLs that are not bundled and are")
        print("not known system DLLs. The installer would fail on a clean machine:\n")
        for dep in sorted(missing):
            importers = ", ".join(sorted(missing[dep]))
            print(f"  MISSING: {dep}   <- imported by: {importers}")
        print("\nFix: bundle the DLL (see the OpenCV install block in")
        print("apps/CMakeLists.txt for the pattern) or stop linking it, then")
        print("rebuild. If a flagged DLL is genuinely a system DLL, add it to")
        print("SYSTEM_DLL_RE in this script.")
        sys.exit(1)

    print(f"OK: all imports of {len(binaries)} staged binaries are bundled or system DLLs.")


def compile_inno_setup(iss_file, project_dir, project_name, defines=None):
    """Compile Inno Setup script.

    defines: optional dict of ISPP #define overrides passed as /DName=Value.
             Each .iss provides #ifndef defaults, so these only need to be
             passed when overriding (as CI/this script does).
    """
    iss_path = project_dir / iss_file

    if not iss_path.exists():
        print(f"ERROR: Inno Setup script not found: {iss_path}")
        sys.exit(1)

    if not INNO_COMPILER.exists():
        print(f"ERROR: Inno Setup compiler not found: {INNO_COMPILER}")
        sys.exit(1)

    # Create output directory if it doesn't exist
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Use /O to override OutputDir, and /D to override source paths in the .iss.
    # Values may contain spaces; subprocess (shell=False) quotes each arg, which
    # is exactly the quoted-switch form iscc expects.
    cmd = [str(INNO_COMPILER), f'/O{OUTPUT_DIR}']
    for key, value in (defines or {}).items():
        cmd.append(f'/D{key}={value}')
    cmd.append(str(iss_path))
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
    parser.add_argument("--skip-configure", action="store_true",
                        help="Skip CMake configure step (WARNING: new/removed source files will not be detected)")
    parser.add_argument("--skip-build", action="store_true", help="Skip CMake compile step")
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

    # Configure step — always runs unless explicitly skipped so that
    # file(GLOB) in CMakeLists picks up any new or removed source files.
    if not args.skip_configure:
        configure_cmake()
    else:
        print("\nSkipping configure step (--skip-configure)")

    # Compile step
    if not args.skip_build:
        build_release()
    else:
        print("\nSkipping build step (--skip-build)")

    # Install step
    if not args.skip_install:
        install_binaries()
    else:
        print("\nSkipping install step (--skip-install)")

    # Gate: every DLL the staged binaries import must be bundled or a system DLL.
    # Catches the class of bug where a dependency resolves on the dev machine
    # (deps bin dir on PATH) but is missing from the installed package.
    verify_dependencies()

    # Inno Setup compilation
    if not args.skip_inno:
        compile_inno_setup(
            "RevereSetup.iss", REVERE_DIR, "Revere",
            defines={"StagingDir": STAGING_DIR, "VCRedistPath": VC_REDIST},
        )
        compile_inno_setup(
            "RevereCloudSetup.iss", REVERE_CLOUD_DIR, "RevereCloud",
            defines={"StagingDir": STAGING_DIR},
        )

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
