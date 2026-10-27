#!/usr/bin/env python3
"""Puts "Claude Buddy Bridge.app" on the Desktop: double-click it to start the
bridge with its status window (buddy_bridge_gui.py). macOS only.

    python3 host/install_shortcut.py            # build it on the Desktop
    python3 host/install_shortcut.py --remove   # delete it again

The app runs this Python (the one you install with), so use one that has Tk.
"""
import os, platform, plistlib, shutil, subprocess, sys, tempfile, tkinter as tk

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from buddy_bridge_gui import clawd_icon

APP = os.path.expanduser("~/Desktop/Claude Buddy Bridge.app")
LOGFILE = os.path.expanduser("~/Library/Logs/claude-buddy-bridge.log")   # the app's stdout/stderr


def make_icns(dest):
    root = tk.Tk(); root.withdraw()
    with tempfile.TemporaryDirectory() as tmp:
        big = os.path.join(tmp, "clawd.png")
        clawd_icon(root, 1024).write(big, format="png")
        iconset = os.path.join(tmp, "clawd.iconset"); os.mkdir(iconset)
        for px in (16, 32, 128, 256, 512):
            for scale, name in ((1, f"icon_{px}x{px}.png"), (2, f"icon_{px}x{px}@2x.png")):
                subprocess.run(["sips", "-z", str(px * scale), str(px * scale), big,
                                "--out", os.path.join(iconset, name)], check=True, capture_output=True)
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", dest], check=True)
    root.destroy()


def main():
    if os.path.exists(APP):
        shutil.rmtree(APP)
    if "--remove" in sys.argv:
        print("removed", APP); return
    macos, res = os.path.join(APP, "Contents", "MacOS"), os.path.join(APP, "Contents", "Resources")
    os.makedirs(macos); os.makedirs(res)
    launcher = os.path.join(macos, "launch")
    with open(launcher, "w") as f:
        # "arch -arm64" picks the Apple silicon half of a universal Python. Without it macOS opens an
        # app bundle's script as Intel under Rosetta ("Support Ending for Intel-based Apps").
        native = "arch -arm64 " if platform.machine() == "arm64" else ""
        f.write(f'#!/bin/sh\nexec {native}"{sys.executable}" "{os.path.join(HERE, "buddy_bridge_gui.py")}" '
                f'> "{LOGFILE}" 2>&1\n')
    os.chmod(launcher, 0o755)
    make_icns(os.path.join(res, "clawd.icns"))
    with open(os.path.join(APP, "Contents", "Info.plist"), "wb") as f:
        plistlib.dump({
            "CFBundleName": "Claude Buddy Bridge",
            "CFBundleDisplayName": "Claude Buddy Bridge",
            "CFBundleIdentifier": "local.claude-desktop-buddy.bridge",
            "CFBundleExecutable": "launch",
            "CFBundleIconFile": "clawd",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": "1.0",
            "NSHighResolutionCapable": True,
        }, f)
    subprocess.run(["touch", APP])   # nudge Finder to pick up the icon
    print("created", APP)


if __name__ == "__main__":
    main()
