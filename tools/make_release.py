"""Packages a release into <build>/release/:

    EchoXR-OpenXR-v<version>.zip          Echo through OpenXR: the launcher and the runtime
    EchoXR-OpenXR-v<version>.zip.sha256   its SHA-256, as `sha256sum` writes it

    python tools/make_release.py                      package the default build
    python tools/make_release.py --build-dir <dir>    package another CMake build tree

The default build tree is build/windows-msvc on Windows and build/cross-clang-cl
elsewhere (the CMake presets). Build it first: cmake --build --preset <name>.

The zip unpacks into Echo VR's bin\\win10 folder, as EchoXR.exe plus one EchoXR\\ folder:

    EchoXR.exe                      the launcher
    EchoXR\\                         runtime, OpenXR loader, notices, README.txt,
                                    echoxr-linux.sh

No game file is included: EchoXR.exe makes echovr_openxr.exe from the player's own
echovr.exe on first run.
"""
import argparse
import hashlib
import os
import sys
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
VERSION = open(os.path.join(ROOT, "VERSION"), encoding="utf-8").read().strip()

# (source relative to the build tree's out/ or, with "@", to ROOT; path inside the zip)
XR_FILES = [
    ("EchoXR.exe",              "EchoXR.exe"),
    ("LibOVRRT64_1.dll",        "EchoXR/LibOVRRT64_1.dll"),
    ("openxr_loader.dll",       "EchoXR/openxr_loader.dll"),
    ("@linux/echoxr-linux.sh",  "EchoXR/echoxr-linux.sh"),
]

# Licences of what the three binaries contain (relative to the build tree, or "@" ROOT).
NOTICES = [
    ("EchoXR's runtime: Revive / ReviveXR (LibreVR) -- MIT License",
     "@xr/revive/LICENSE"),
    ("OpenXR SDK and loader (The Khronos Group) -- Apache License 2.0",
     "_deps/openxr_sdk-src/LICENSES/Apache-2.0.txt"),
    ("JsonCpp, inside the OpenXR loader -- MIT License / public domain",
     "_deps/openxr_sdk-src/src/external/jsoncpp/LICENSE"),
    ("Microsoft Detours -- MIT License",
     "_deps/detours-src/LICENSE"),
]

# --- README text, shared pieces -------------------------------------------------------
LINUX = """Linux
-----
EchoXR runs on Linux through Proton, on SteamVR, Monado or WiVRn. Copy Echo VR
(the whole ready-at-dawn-echo-arena folder) from a Windows PC, unzip this release
into its bin/win10 folder as above, and copy LibOVRPlatform64_1.dll and
LibOVRP2P64_1.dll from the PC's C:\\Program Files\\Oculus\\Support\\oculus-runtime\\
next to echovr.exe. Start your VR service, wake the headset, then run:

   bin/win10/EchoXR/echoxr-linux.sh --check     reports anything missing
   bin/win10/EchoXR/echoxr-linux.sh             starts Echo

It needs Steam with Proton 9 or newer, Proton Experimental or GE-Proton, and an active
OpenXR runtime. No OpenVR runtime (xrizer, OpenComposite) is needed.

The Echo VR launcher does all of this for you, on Windows and Linux.
"""

INSTALL_XR = """Install
-------
1. Unzip into Echo VR's bin\\win10 folder, the one with echovr.exe, e.g.
   C:\\Program Files\\Oculus\\Software\\Software\\ready-at-dawn-echo-arena\\bin\\win10
   You should end up with EchoXR.exe next to echovr.exe, and an EchoXR folder.
2. Install SteamVR (free, on Steam) and check your headset works in it.
3. Run EchoXR.exe. It has no window: Echo starts, or launcher.log says why not.

The first launch makes echovr_openxr.exe, a patched copy of your echovr.exe that
accepts the EchoXR runtime (echovr.exe itself isn't changed).
"""

# --- the README -------------------------------------------------------------------
README = ("""EchoXR {version}
===========
Echo VR through OpenXR: on SteamVR on Windows, and through Proton on Linux.
No Oculus or Meta app, no Visual C++ runtime.

""" + INSTALL_XR + "\n" + LINUX + """
Logs
----
EchoXR\\launcher.log        every launch: the runtime and headset found, Echo's exit code
EchoXR\\runtime.log         the OpenXR side, including any failed OpenXR call

EchoXR.exe's own exit codes: 2 not in bin\\win10, 3 runtime files missing,
4 echovr_openxr.exe couldn't be made, 5 no OpenXR runtime (or VR service),
6 no headset, 7 Echo couldn't start. Anything else is Echo's own.

Licences: see EchoXR\\THIRD_PARTY_NOTICES.txt.
""")

def source(build, src):
    return os.path.join(ROOT, src[1:]) if src.startswith("@") else os.path.join(build, "out", src)


def notices(build):
    parts = ["EchoXR third-party notices", ""]
    for title, src in NOTICES:
        path = os.path.join(ROOT, src[1:]) if src.startswith("@") else os.path.join(build, src)
        with open(path, encoding="utf-8", errors="replace") as f:
            parts += ["==== %s ====" % title, f.read().strip(), ""]
    return "\n".join(parts)


def write_zip(path, build, readme):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        # An explicit folder entry, so every unzip tool creates EchoXR\ next to EchoXR.exe,
        # with permissions that let Linux and macOS open it.
        folder = zipfile.ZipInfo("EchoXR/")
        folder.external_attr = (0o40755 << 16) | 0x10
        z.writestr(folder, "")
        for src, dst in XR_FILES:
            info = zipfile.ZipInfo.from_file(source(build, src), dst)
            info.compress_type = zipfile.ZIP_DEFLATED
            exe = dst.endswith((".sh", ".exe"))
            info.external_attr = (0o100755 if exe else 0o100644) << 16
            with open(source(build, src), "rb") as f:
                z.writestr(info, f.read())
        for name, text in (("EchoXR/THIRD_PARTY_NOTICES.txt", notices(build)),
                           ("EchoXR/README.txt", readme.format(version="v" + VERSION))):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, text.replace("\r\n", "\n").replace("\n", "\r\n"))


def main():
    default = "windows-msvc" if os.name == "nt" else "cross-clang-cl"
    ap = argparse.ArgumentParser(description="Package an EchoXR release from a CMake build tree.")
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build", default))
    build = os.path.abspath(ap.parse_args().build_dir)
    missing = [source(build, src) for src, _ in XR_FILES if not os.path.isfile(source(build, src))]
    if missing:
        sys.exit("missing build outputs (build first):\n  " + "\n  ".join(missing))
    out = os.path.join(build, "release")
    os.makedirs(out, exist_ok=True)
    made = os.path.join(out, "EchoXR-OpenXR-v%s.zip" % VERSION)
    write_zip(made, build, README)
    digest = hashlib.sha256(open(made, "rb").read()).hexdigest()
    with open(made + ".sha256", "w", newline="\n") as f:
        f.write("%s  %s\n" % (digest, os.path.basename(made)))
    print("%s  %.1f KB  sha256 %s" % (made, os.path.getsize(made) / 1024, digest))


if __name__ == "__main__":
    main()
