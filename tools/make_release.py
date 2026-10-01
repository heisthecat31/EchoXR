"""Builds the release into out/release/:

    EchoXR-OpenXR-v<version>.zip   Echo on SteamVR through OpenXR: the launcher and the
                                   OpenXR translation layer

    python tools/make_release.py              build, then package
    python tools/make_release.py --no-build   package what's already built (xr/out)

The zip unpacks into Echo VR's bin\\win10 folder, as EchoXR.exe plus one EchoXR\\ folder:

    EchoXR.exe                      the launcher
    EchoXR\\                         runtime, OpenXR loader, notices, README.txt,
                                    echoxr-linux.sh

No game file is included: EchoXR.exe makes echovr_openxr.exe from the player's own
echovr.exe on first run. echoxr.ini isn't shipped either, so unzipping a newer release
never resets the player's settings.
"""
import os
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
    ("xr/out/THIRD_PARTY_NOTICES.txt",             "EchoXR/THIRD_PARTY_NOTICES.txt"),
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
installing it. Turn that off with CheckForUpdates = 0 in EchoXR\\echoxr.ini.
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
   EchoXR runtime (echovr.exe itself isn't changed).
"""

# --- the README -------------------------------------------------------------------
README = ("""EchoXR {version}
===========
Echo VR on SteamVR through OpenXR (no Oculus app).

""" + INSTALL_XR + "\n" + LINUX + "\n" + UPDATES + """
Logs
----
EchoXR\\launcher.log        what EchoXR.exe set up and launched
EchoXR\\runtime.log         the OpenXR side, including any failed OpenXR call

Licences: see EchoXR\\THIRD_PARTY_NOTICES.txt.
""")


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
        subprocess.check_call(["cmd", "/c", os.path.join(ROOT, "build.bat")], cwd=ROOT)
    missing = [src for src, _ in XR_FILES if not os.path.isfile(os.path.join(ROOT, src))]
    if missing:
        sys.exit("missing build outputs:\n  " + "\n  ".join(missing))
    out = os.path.join(ROOT, "out", "release")
    os.makedirs(out, exist_ok=True)
    made = os.path.join(out, "EchoXR-OpenXR-v%s.zip" % VERSION)
    write_zip(made, XR_FILES, ("EchoXR/",), ("EchoXR/README.txt", README))
    print("%-60s %8.1f KB" % (os.path.relpath(made, ROOT), os.path.getsize(made) / 1024))


if __name__ == "__main__":
    main()
