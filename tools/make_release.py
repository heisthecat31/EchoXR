"""Builds the release into out/release/:

    EchoXR-v<version>.zip          EchoXR and EchoXR Hands (what the auto-updater fetches)
    EchoXR-OpenXR-v<version>.zip   EchoXR only: Echo on SteamVR through OpenXR, no hand tracking
    EchoXR-Hands-v<version>.zip    EchoXR Hands only: hand tracking plugin, bridge and settings
    EchoXRSetup-v<version>.exe     the installer (either or both, by switch)

    python tools/make_release.py              build everything, then package
    python tools/make_release.py --no-build   package what's already built

Every zip unpacks into Echo VR's bin\\win10 folder. The full one, as EchoXR.exe plus one
EchoXR\\ folder:

    EchoXR.exe                      the launcher
    EchoXR\\                         runtime, OpenXR loader, notices, README.txt
    EchoXR\\Hands\\                   finger bridge (EchoXRHands.exe) and its SteamVR files
    EchoXR\\Hands\\install\\           hand tracking plugin, default settings, plugin loader;
                                    EchoXR.exe (or, without it, EchoXRHands.exe) puts these
                                    into the game folder

The OpenXR package is the first two lines; the Hands package is the EchoXR\\Hands\\ part.
An OpenXR-only install updates from the OpenXR package (xr/src/updater.h), so an update
never adds hand tracking.

No game file is included: EchoXR.exe makes echovr_openxr.exe from the player's own
echovr.exe on first run. echoxr.ini isn't shipped either, so unzipping a newer release
never resets the player's settings; EchoXR.exe writes a default one.
"""
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
VERSION = open(os.path.join(ROOT, "VERSION"), encoding="utf-8").read().strip()

# (source relative to ROOT, path inside the zip)
XR_FILES = [   # the OpenXR translation layer and its launcher
    ("xr/out/EchoXR.exe",                          "EchoXR.exe"),
    ("xr/out/LibOVRRT64_1.dll",                    "EchoXR/LibOVRRT64_1.dll"),
    ("xr/out/openxr_loader.dll",                   "EchoXR/openxr_loader.dll"),
    ("installer/stage/THIRD_PARTY_NOTICES.txt",    "EchoXR/THIRD_PARTY_NOTICES.txt"),
    ("linux/echoxr-linux.sh",                      "EchoXR/echoxr-linux.sh"),
]
HANDS_FILES = [   # hand tracking
    ("out/EchoXRHands.exe",                        "EchoXR/Hands/EchoXRHands.exe"),
    ("out/EchoXRSettings.exe",                     "EchoXR/Hands/EchoXRSettings.exe"),
    ("out/htv_actions.json",                       "EchoXR/Hands/htv_actions.json"),
    ("out/htv_bindings_knuckles.json",             "EchoXR/Hands/htv_bindings_knuckles.json"),
    ("xr/Revive/Externals/openvr/bin/win64/openvr_api.dll", "EchoXR/Hands/openvr_api.dll"),
    ("tools/fake_index.py",                        "EchoXR/Hands/fake_index.py"),
    ("out/EchoXRHands.dll",                        "EchoXR/Hands/install/EchoXRHands.dll"),
    ("out/EchoXRHands.txt",                        "EchoXR/Hands/install/EchoXRHands.txt"),
    ("installer/stage/dbgcore.dll",                "EchoXR/Hands/install/dbgcore.dll"),
]

# --- README text, shared pieces -------------------------------------------------------
LINUX = """Linux
-----
EchoXR runs on Linux through Proton (untested so far), on SteamVR, Monado or
WiVRn. Copy Echo VR (the whole ready-at-dawn-echo-arena folder) from a Windows
PC, unzip this release into its bin/win10 folder as above, start your VR server,
then run:

   bin/win10/EchoXR/echoxr-linux.sh --check     reports anything missing
   bin/win10/EchoXR/echoxr-linux.sh             starts Echo

It needs Steam with Proton Experimental, Proton 8+ or GE-Proton, an active
OpenXR runtime, and an OpenVR runtime: SteamVR, or xrizer/OpenComposite on
Monado and WiVRn. Proton only turns OpenXR on when OpenVR is there.
"""

UPDATES = """Updates
-------
EchoXR.exe checks GitHub for a new release once a day and asks before
installing it. Turn that off with CheckForUpdates = 0 in EchoXR\\echoxr.ini.
"""

HAND_TRACKING = """Hand tracking
-------------
In-game, hold both hands fully open once: that calibrates the open-hand pose.
Press Ctrl+Alt+C (hands open) to recalibrate at any time.
Valve Index controllers give full finger sensing; other controllers and
controller-free hand tracking use the SteamVR hand skeleton.
Settings: run EchoXR\\Hands\\EchoXRSettings.exe. Changes show in-game as you make
them; they're stored in bin\\win10\\plugins\\EchoXRHands.txt (re-read while the game runs).
Finger sharing: the plugin sends your fingers through a relay so other players
running it can see them, and shows theirs (Network = 0 turns this off; it sends
your display name and your match's player names so the relay can pair you up).
Sending works; seeing another player's fingers hasn't been confirmed in a match yet.
"""

INSTALL_XR = """Install
-------
1. Unzip into Echo VR's bin\\win10 folder, the one with echovr.exe, e.g.
   C:\\Program Files\\Oculus\\Software\\Software\\ready-at-dawn-echo-arena\\bin\\win10
   You should end up with EchoXR.exe next to echovr.exe, and an EchoXR folder.
2. Install SteamVR (free, on Steam) and check your headset works in it.
3. Run EchoXR.exe.

The first launch sets things up:
 - makes echovr_openxr.exe, a patched copy of your echovr.exe that accepts the
   EchoXR runtime (echovr.exe itself isn't changed);
"""

INSTALL_HANDS_WITH_XR = """ - puts the hand tracking plugin in bin\\win10\\plugins\\ and, if needed, the plugin
   loader (dbgcore.dll). An older 45 KB dbgcore.dll is moved to
   plugins\\dbgcore_legacy.dll and keeps loading. A different plugin loader is kept.
   If dbgcore.dll is something else entirely, it's left alone and hand tracking isn't
   installed; run EchoXRSetup.exe instead.
 - starts the finger bridge (EchoXR\\Hands\\EchoXRHands.exe) with Echo and stops it
   when Echo closes. Turn this off with AutoStartHands = 0 in EchoXR\\echoxr.ini.
"""

# --- the three READMEs ----------------------------------------------------------------
README = ("""EchoXR {version}
===========
Echo VR on SteamVR through OpenXR (no Oculus app), with per-finger Valve Index
hand tracking.

""" + INSTALL_XR + INSTALL_HANDS_WITH_XR + "\n" + HAND_TRACKING + "\n" + LINUX + "\n" + UPDATES + """
Logs
----
EchoXR\\launcher.log        what EchoXR.exe set up and launched
EchoXR\\runtime.log         the OpenXR side, including any failed OpenXR call
plugins\\EchoXRHands.log    the hand tracking plugin

Licences: see EchoXR\\THIRD_PARTY_NOTICES.txt.
""")

README_XR = ("""EchoXR {version} (OpenXR only)
===========================
Echo VR on SteamVR through OpenXR (no Oculus app). This package has no hand
tracking: EchoXR-Hands-{version}.zip adds it, or get EchoXR-{version}.zip for both.

""" + INSTALL_XR + "\n" + LINUX + "\n" + UPDATES + """(An OpenXR-only install updates from the OpenXR-only package.)

Logs
----
EchoXR\\launcher.log        what EchoXR.exe set up and launched
EchoXR\\runtime.log         the OpenXR side, including any failed OpenXR call

Licences: see EchoXR\\THIRD_PARTY_NOTICES.txt.
""")

README_HANDS = ("""EchoXR Hands {version} (hand tracking only)
========================================
Per-finger hand tracking for Echo VR: your own fingers on your chassis hands, and
other players'. Works however you run Echo: the Oculus / Meta runtime, or EchoXR
(EchoXR-OpenXR-{version}.zip adds it, or get EchoXR-{version}.zip for both).

Install
-------
1. Unzip into Echo VR's bin\\win10 folder, the one with echovr.exe. You should end
   up with an EchoXR\\Hands folder there.
2. Install SteamVR (free, on Steam): the finger bridge reads your fingers from it.
3. Run EchoXR\\Hands\\EchoXRHands.exe. Each start, it puts the hand tracking plugin
   into bin\\win10\\plugins\\ and, if needed, the plugin loader (dbgcore.dll). An
   older 45 KB dbgcore.dll moves to plugins\\dbgcore_legacy.dll and keeps loading;
   a different plugin loader is kept; anything else is left alone (run
   EchoXRSetup.exe then). EchoXRHands.exe --setup does only this and exits.
4. Start Echo VR while the bridge is running.

""" + HAND_TRACKING + """
Log: plugins\\EchoXRHands.log. openvr_api.dll is Valve's (BSD-3-Clause).
""")

PACKAGES = [   # (zip name, files, folder entries, (readme path, text))
    ("EchoXR-v%s.zip", XR_FILES + HANDS_FILES, ("EchoXR/", "EchoXR/Hands/", "EchoXR/Hands/install/"),
     ("EchoXR/README.txt", README)),
    ("EchoXR-OpenXR-v%s.zip", XR_FILES, ("EchoXR/",), ("EchoXR/README.txt", README_XR)),
    ("EchoXR-Hands-v%s.zip", HANDS_FILES, ("EchoXR/", "EchoXR/Hands/", "EchoXR/Hands/install/"),
     ("EchoXR/Hands/README.txt", README_HANDS)),
]


def write_zip(path, files, folders, readme):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        # explicit folder entries, so every unzip tool creates EchoXR\ next to EchoXR.exe
        for d in folders:
            z.writestr(zipfile.ZipInfo(d), "")
        for src, dst in files:
            info = zipfile.ZipInfo.from_file(os.path.join(ROOT, src), dst)
            info.compress_type = zipfile.ZIP_DEFLATED
            if dst.endswith(".sh"):
                info.external_attr = 0o100755 << 16      # executable once unzipped on Linux
            with open(os.path.join(ROOT, src), "rb") as f:
                z.writestr(info, f.read())
        z.writestr(readme[0], readme[1].format(version="v" + VERSION).replace("\n", "\r\n"))


def main():
    if "--no-build" not in sys.argv:
        subprocess.check_call(["cmd", "/c", os.path.join(ROOT, "installer", "build_installer.bat")], cwd=ROOT)
    missing = [src for src, _ in XR_FILES + HANDS_FILES if not os.path.isfile(os.path.join(ROOT, src))]
    if missing:
        sys.exit("missing build outputs:\n  " + "\n  ".join(missing))
    out = os.path.join(ROOT, "out", "release")
    os.makedirs(out, exist_ok=True)
    made = []
    for name, files, folders, readme in PACKAGES:
        made.append(os.path.join(out, name % VERSION))
        write_zip(made[-1], files, folders, readme)
    setup = os.path.join(out, "EchoXRSetup-v%s.exe" % VERSION)
    shutil.copyfile(os.path.join(ROOT, "out", "EchoXRSetup.exe"), setup)
    for p in made + [setup]:
        print("%-60s %8.1f KB" % (os.path.relpath(p, ROOT), os.path.getsize(p) / 1024))


if __name__ == "__main__":
    main()
