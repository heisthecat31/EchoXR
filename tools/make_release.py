"""Builds the EchoXR release into out/release/, for github.com/heisthecat31/EchoXR:

    EchoXR-v<VERSION>.zip            Echo on SteamVR through OpenXR (what the auto-updater fetches)
    EchoXRSetup-v<VERSION>.exe       the installer: EchoXR, hand tracking, or both

    python tools/make_release.py              build everything, then package
    python tools/make_release.py --no-build   package what's already built

The zip unpacks into Echo VR's bin\\win10 folder, as EchoXR.exe (the launcher) plus an
EchoXR\\ folder (runtime, OpenXR loader, notices, README.txt).

Hand tracking is its own repository and release (github.com/heisthecat31/EchoXR-Hands,
which has its own make_release.py). EchoXR.exe installs and updates it from there
(xr/src/updater.h); the installer bundles it, built from that repository checked out
next to this one.

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
installing it, and does the same for EchoXR Hands when hand tracking is on.
Turn that off with CheckForUpdates = 0 in EchoXR\\echoxr.ini.
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

# --- the README -------------------------------------------------------------------
README_XR = ("""EchoXR {version}
===========
Echo VR on SteamVR through OpenXR (no Oculus app).

""" + INSTALL_XR + """
Hand tracking
-------------
EchoXR Hands, per-finger hand tracking, is released separately:
https://github.com/heisthecat31/EchoXR-Hands/releases
To have EchoXR install it, set AutoStartHands = 1 in EchoXR\\echoxr.ini. The next
launch of EchoXR.exe downloads the latest EchoXR Hands, puts the plugin in place,
and runs the finger bridge alongside Echo. If hand tracking is already installed,
EchoXR.exe switches this on by itself on first launch.

""" + LINUX + "\n" + UPDATES + """
Logs
----
EchoXR\\launcher.log        what EchoXR.exe set up and launched, and hand tracking downloads
EchoXR\\runtime.log         the OpenXR side, including any failed OpenXR call

Licences: see EchoXR\\THIRD_PARTY_NOTICES.txt.
""")


PACKAGES = [   # (zip name, files, folder entries, extra files {path: text})
    ("EchoXR-v%s.zip", XR_FILES, ("EchoXR/",), {"EchoXR/README.txt": README_XR}),
]


def write_zip(path, version, files, folders, extra):
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
        for name, text in extra.items():
            z.writestr(name, text.format(version="v" + version).replace("\n", "\r\n"))


def main():
    if "--no-build" not in sys.argv:
        subprocess.check_call(["cmd", "/c", os.path.join(ROOT, "installer", "build_installer.bat")], cwd=ROOT)
    missing = [src for src, _ in XR_FILES if not os.path.isfile(os.path.join(ROOT, src))]
    if missing:
        sys.exit("missing build outputs:\n  " + "\n  ".join(missing))
    out = os.path.join(ROOT, "out", "release")
    os.makedirs(out, exist_ok=True)
    made = []
    for name, files, folders, extra in PACKAGES:
        made.append((os.path.join(out, name % VERSION), "heisthecat31/EchoXR"))
        write_zip(made[-1][0], VERSION, files, folders, extra)
    setup = os.path.join(out, "EchoXRSetup-v%s.exe" % VERSION)
    shutil.copyfile(os.path.join(ROOT, "out", "EchoXRSetup.exe"), setup)
    made.append((setup, "heisthecat31/EchoXR"))
    for p, repo in made:
        print("%-52s %8.1f KB  -> %s" % (os.path.relpath(p, ROOT), os.path.getsize(p) / 1024, repo))


if __name__ == "__main__":
    main()
