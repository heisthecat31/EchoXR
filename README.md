<img src="logo/echoxr.png" width="96" alt="EchoXR logo">

# EchoXR

EchoXR runs **Echo VR on SteamVR through OpenXR**, with no Oculus app in the path and
no injection. Echo's Oculus calls (LibOVR) are answered by ReviveXR over OpenXR, pinned
to SteamVR for each launch. It works with any headset SteamVR supports.

This is the EchoTools fork of [heisthecat31/EchoXR](https://github.com/heisthecat31/EchoXR)
with only the OpenXR layer: the hand tracking (EchoXR Hands) and the installer are not
part of it. The details of the layer are in [xr/README.md](xr/README.md).

## What it does

| | |
| --- | --- |
| **Echo on SteamVR** | `EchoXR.exe` starts Echo with the EchoXR runtime: Revive's OpenXR backend under the Oculus runtime's DLL name, and the Khronos OpenXR loader. |
| **No game file changed** | It runs `echovr_openxr.exe`, a copy of `echovr.exe` made on the player's machine whose runtime signature check passes. `echovr.exe` itself is never touched. |
| **SteamVR per launch** | Each launch is pinned to SteamVR's OpenXR runtime, even when another app made itself the system runtime. `--runtime active` uses the system runtime instead. |
| **Updates** | `EchoXR.exe` checks this repository's releases for a newer `EchoXR-OpenXR-v*.zip` once a day, and offers to install it. |

### Limits

- **One game build.** The patch for `echovr_openxr.exe` is for the current `echovr.exe`
  (35,397,120 bytes, May 2023). It checks the bytes it changes first, and refuses
  anything else instead of breaking the game.

## Install

The release zip `EchoXR-OpenXR-v<version>.zip` unpacks into Echo's `bin\win10` folder,
the one with `echovr.exe`:

```
bin\win10\
  EchoXR.exe                  the launcher
  EchoXR\                     OpenXR runtime, loader, licences, README.txt, echoxr-linux.sh
```

1. **Install SteamVR** (free, in Steam) and check that your headset shows up in it.
2. **Unzip the release** into the Echo install's `bin\win10` folder.
3. **Run** `bin\win10\EchoXR.exe` with SteamVR running. On its first launch it makes
   `echovr_openxr.exe` from your `echovr.exe`.

`EchoXR\echoxr.ini` holds the launcher's settings: `CheckForUpdates = 0` turns the
update check off, and `EchoXR.exe --check-update` checks straight away.

## Linux (untested)

The release zip also runs on Linux through Proton. It uses the Proton that Steam
already has, and Steam's Linux runtime container when that's installed. There's
no other app to install.

1. Copy the whole `ready-at-dawn-echo-arena` folder from a Windows PC.
2. Unzip the release into its `bin/win10` folder.
3. Copy `LibOVRPlatform64_1.dll` and `LibOVRPlatformImpl64_1.dll` from the Windows
   PC's `C:\Program Files\Oculus\Support\oculus-runtime\` into `bin/win10`.
   `pnsovr.dll` needs them to log in, and a Linux prefix has no Oculus folder.
4. Set an active OpenXR runtime (SteamVR, Monado or WiVRn). Proton also needs an
   **OpenVR** runtime, registered in `~/.config/openvr/openvrpaths.vrpath`, before
   it turns OpenXR on for a game. SteamVR registers itself. For Monado or WiVRn,
   install [xrizer](https://github.com/Supreeeme/xrizer) or OpenComposite and
   register it (WiVRn and Envision can do this for you).
5. Start the VR server (SteamVR, `monado-service` or `wivrn-server`) and wake the
   headset. Proton checks VR once, when Echo starts; if the server isn't up then,
   VR stays off for that launch.
6. Run `bin/win10/EchoXR/echoxr-linux.sh --check`. It reports what it found and
   what's missing, without starting Echo. Then run it without `--check`.

The VR path stays inside the game process:

```
echovr_openxr.exe -> EchoXR\LibOVRRT64_1.dll -> EchoXR\openxr_loader.dll
  -> Proton's wineopenxr.dll (the prefix's registered OpenXR runtime)
  -> wineopenxr.so -> the Linux runtime in XR_RUNTIME_JSON
```

Under Wine, `EchoXR.exe` notices `wine_get_version` and leaves the runtime
choice to Proton instead of pinning SteamVR's Windows manifest. The script:

- **Picks Proton:** the newest GE-Proton, then Proton Experimental, then the
  newest `Proton N`. It refuses one without `wineopenxr`.
- **Picks the runtime:** `XR_RUNTIME_JSON`, or your active one in
  `~/.config/openxr/1/active_runtime.json`.
- **Checks OpenVR:** that a runtime is registered and has
  `bin/linux64/vrclient.so`, the file Proton loads (`VR_OVERRIDE` picks another).
  It warns when the VR server isn't running, or when OpenVR is SteamVR's but
  OpenXR is another runtime, since SteamVR then has to run too.
- **Uses its own prefix:** `~/.local/share/echoxr/prefix`.
- **Sets Proton up:** the `STEAM_COMPAT_*` variables, plus
  `PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1` so the container can see the
  runtime.
- **Keeps the game's plugin loader:** `WINEDLLOVERRIDES=dbgcore=n,b`, because Wine
  would otherwise load its own `dbgcore.dll` instead of the one in `bin/win10`.

`ECHOXR_PROTON`, `ECHOXR_PREFIX`, `ECHOXR_NO_CONTAINER=1` and `ECHOXR_DEBUG=1`
override the choices. `ECHOXR_DEBUG=1` also writes Proton and OpenXR loader logs
to `~/.local/share/echoxr/logs/`. The script's own output goes to
`~/.local/share/echoxr/echoxr-linux.log`, and when Echo exits it lists every log
to send with a bug report.

The approach follows [RiftLift](https://github.com/Villagers654/RiftLift), which
runs Rift games the same way. No RiftLift code is used: it's GPL-3.0.

Nothing here has been run on Linux yet. The open questions are:

- whether Echo's renderer works with Proton's `wineopenxr` (the Windows logs show
  a D3D12 device);
- whether `pnsovr.dll` and the Platform SDK DLLs log in under Wine;

## Logs

| log | what's in it |
| --- | --- |
| `EchoXR\launcher.log` | first-run setup, the runtime chosen, Echo's exit code |
| `EchoXR\runtime.log` | the OpenXR runtime and its extensions, and every failed OpenXR call |
| `_local\r14logs\*.log` | Echo's own log ("Initializing OVR session…" and any session error) |

## Building

Everything builds with MSVC (Visual Studio 2026 toolset).

| command | builds |
| --- | --- |
| `build.bat` or `xr\build_xr.bat` | `xr\out\LibOVRRT64_1.dll`, `openxr_loader.dll`, `EchoXR.exe` and `THIRD_PARTY_NOTICES.txt`. It needs three upstream checkouts plus a patch; [xr/README.md](xr/README.md) has the exact commits and commands |
| `xr\build_launcher.bat` | just `xr\out\EchoXR.exe` (quick) |
| `python tools\make_release.py [--no-build]` | `out\release\EchoXR-OpenXR-v<VERSION>.zip` |
| `python tools\gen_logo.py` | the logo in `logo\` (SVG, PNG, ICO) |

The version number is in `VERSION`. `linux\echoxr-linux.sh` needs no build; the release
zip ships it as `EchoXR/echoxr-linux.sh`.

| folder | what |
| --- | --- |
| `xr/` | EchoXR runtime glue and launcher (`src/`), Revive patch (`patches/`) |
| `linux/` | Linux launcher script |
| `logo/` | the logo |
| `third_party/` | Microsoft Detours header (ReviveXR's D3D code) |
| `tools/` | release packaging, logo generator |

## Credits and licences

- **EchoXR** by heisthecat31; this fork keeps only its OpenXR layer.
- **Revive / ReviveXR**: LibreVR, MIT License.
- **OpenXR SDK and loader**: The Khronos Group, Apache 2.0.
- **Oculus PC SDK headers**: Oculus/Meta. Used at build time only.
- **Microsoft Detours**: MIT License.
