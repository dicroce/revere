#!/usr/bin/env python3

import os
import shutil
import subprocess
from pathlib import Path
from PIL import Image  # Requires `pip install pillow`

# Configuration
APPDIR = Path("revere.AppDir")
USR_BIN = APPDIR / "usr" / "bin"
USR_LIB = APPDIR / "usr" / "lib"
INSTALL_PREFIX = Path("/usr/local/revere")
EXECUTABLES = ["revere", "vision"]
GSTREAMER_DIR = INSTALL_PREFIX / "gstreamer_plugins"
MOTION_PLUGINS_DIR = INSTALL_PREFIX / "motion_plugins"
MODELS_DIR = INSTALL_PREFIX / "models"
ICONS = {
    "revere": INSTALL_PREFIX / "R.png",
    "vision": INSTALL_PREFIX / "V.png",
}
APP_NAME = "Revere"
DESKTOP_ENTRY_NAME = "revere.desktop"

def run(cmd):
    result = subprocess.run(cmd, shell=True, check=True, text=True, capture_output=True)
    return result.stdout.strip()

def copy_dependencies(binary_path, lib_dest):
    """Copy non-system shared libraries into usr/lib"""
    ldd_output = run(f"ldd {binary_path}")
    for line in ldd_output.splitlines():
        parts = line.strip().split("=>")
        if len(parts) < 2:
            continue
        lib_path = parts[1].split()[0]
        
        # Skip system libraries but include our project libraries and third-party deps
        if (lib_path.startswith("/lib") or lib_path.startswith("/usr/lib")) and \
           not any(x in lib_path for x in ["gstreamer", "opencv", "ffmpeg"]):
            # Skip core system libraries
            if any(x in lib_path for x in ["libc.so", "libm.so", "ld-linux", "libdl.so", "libpthread.so"]):
                continue
        
        target = lib_dest / os.path.basename(lib_path)
        if not target.exists():
            print(f"Copying dependency {lib_path}")
            shutil.copy2(lib_path, target)

def copy_project_libraries(lib_dest):
    """Copy project's shared libraries from the installation directory"""
    project_libs = [
        "libr_ui_utils.so",
        "libr_utils.so", 
        "libr_av.so",
        "libr_db.so",
        "libr_disco.so",
        "libr_fakey.so",
        "libr_http.so",
        "libr_motion.so",
        "libr_onvif.so",
        "libr_pipeline.so",
        "libr_storage.so",
        "libr_vss.so",
        "libimgui.so"
    ]
    
    for lib_name in project_libs:
        src = INSTALL_PREFIX / lib_name
        if src.exists():
            dst = lib_dest / lib_name
            print(f"Copying project library {lib_name}")
            shutil.copy2(src, dst)
        else:
            print(f"⚠️  Warning: Project library {src} not found")

def place_icon(name: str, source: Path):
    """Detect icon size and place in appropriate hicolor subdir"""
    try:
        with Image.open(source) as img:
            width, height = img.size
            if width != height:
                print(f"⚠️  Warning: Icon {source} is not square ({width}x{height})")
            size_folder = f"{width}x{height}"
    except Exception as e:
        print(f"❌ Failed to open icon {source}: {e}")
        return

    icon_dest_dir = APPDIR / "usr" / "share" / "icons" / "hicolor" / size_folder / "apps"
    icon_dest_dir.mkdir(parents=True, exist_ok=True)
    dst = icon_dest_dir / f"{name}.png"
    print(f"✅ Copying icon {source} → {dst}")
    shutil.copy2(source, dst)

def main():
    print("🔧 Setting up AppDir...")

    if APPDIR.exists():
        shutil.rmtree(APPDIR)
    USR_BIN.mkdir(parents=True)
    USR_LIB.mkdir(parents=True)

    # Copy project shared libraries FIRST so they're available for dependency resolution
    print("📚 Copying project shared libraries")
    copy_project_libraries(USR_LIB)

    for exe in EXECUTABLES:
        src = INSTALL_PREFIX / exe
        dst = USR_BIN / exe
        print(f"🗂️  Copying binary {exe}")
        shutil.copy2(src, dst)
        os.chmod(dst, 0o755)
        copy_dependencies(src, USR_LIB)

    print("📁 Copying GStreamer plugins")
    shutil.copytree(GSTREAMER_DIR, USR_BIN / "gstreamer_plugins")
    
    print("📁 Copying motion plugins")
    if MOTION_PLUGINS_DIR.exists():
        shutil.copytree(MOTION_PLUGINS_DIR, USR_BIN / "motion_plugins")
    else:
        print(f"⚠️  Warning: Motion plugins directory {MOTION_PLUGINS_DIR} not found")
    
    print("📁 Copying models")
    if MODELS_DIR.exists():
        shutil.copytree(MODELS_DIR, USR_BIN / "models")
    else:
        print(f"⚠️  Warning: Models directory {MODELS_DIR} not found")

    # Create AppRun
    apprun_path = APPDIR / "AppRun"
    apprun_path.write_text("#!/bin/bash\nexec \"$(dirname \"$0\")/usr/bin/revere\" \"$@\"\n")
    apprun_path.chmod(0o755)

    # Desktop entry
    desktop_file = APPDIR / DESKTOP_ENTRY_NAME
    desktop_file.write_text(f"""
[Desktop Entry]
Version=1.0
Type=Application
Name=Revere Surveillance
Exec=revere
Comment=An open source video surveillance application
Icon=revere
Terminal=false
Categories=Utility;GTK;
""".strip())

    # Place icons
    for name, path in ICONS.items():
        if path.exists():
            place_icon(name, path)
        else:
            print(f"⚠️  Warning: Icon file {path} not found")

    print("\n✅ AppDir ready! You can now run:")
    print(f"  LD_LIBRARY_PATH={USR_LIB}:{INSTALL_PREFIX} ./linuxdeploy-x86_64.AppImage --appdir {APPDIR} --desktop-file {desktop_file} --output appimage")
    print("\nNote: The LD_LIBRARY_PATH includes both the AppDir lib directory and the installation directory")

if __name__ == "__main__":
    main()
