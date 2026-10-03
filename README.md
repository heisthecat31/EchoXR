<img src="installer/logo/echoxr.png" width="96" alt="EchoXR logo">

# EchoXR

EchoXR runs **Echo VR on SteamVR through OpenXR**, with no Oculus app in the
path. It works with any headset SteamVR supports.

Hand tracking is a separate project, **[EchoXR Hands](https://github.com/heisthecat31/EchoXR-Hands)**: per-finger hand
tracking on Echo's chassis hands, with its own repository, README and
[releases](https://github.com/heisthecat31/EchoXR-Hands/releases).
EchoXR can install it and keep it updated for you; see [Hand tracking](#hand-tracking).

## What it does

| | |
| --- | --- |
| **Echo on SteamVR** | Echo's Oculus calls (LibOVR) are answered by ReviveXR over OpenXR, pinned to SteamVR for each launch. Details: [xr/README.md](xr/README.md). |
| **Hand tracking, if you want it** | With `AutoStartHands = 1`, `EchoXR.exe` downloads EchoXR Hands from its latest release, runs the finger bridge alongside Echo, and offers hand tracking updates. |
| **Updates** | `EchoXR.exe` checks GitHub for a new EchoXR release once a day, and offers to install it. |
| **Install and update** | `EchoXRSetup.exe` is an installer with a GUI. The release zip needs nothing more than unzipping; `EchoXR.exe` does the setup on first launch. |
| **Linux (experimental)** | `echoxr-linux.sh` runs Echo through Proton on SteamVR, Monado or WiVRn. |

<img src="docs/screenshots/setup.png" width="380" alt="EchoXRSetup: game folder, and a switch for each part">

### What has been seen working

From the logs of real sessions on the current Echo build:

- **Echo on SteamVR:** Echo starts and runs a full session on SteamVR/OpenXR
  2.17.10, with zero failed OpenXR calls.
- **Steam Frame:** the headset and its controllers track in-game (since v0.2.1).
- **Updates:** `EchoXR.exe` found the `v0.1.0` GitHub release, downloaded it and
  installed it over an older build.

### Limits

- **One game build.** The patch for `echovr_openxr.exe` is for the current
  `echovr.exe` (35,397,120 bytes, May 2023). It checks the bytes it changes
  first, and refuses anything else instead of breaking the game.

## Install

### Release zip

`EchoXR-v<version>.zip` unpacks into Echo's `bin\win10` folder, the one with
`echovr.exe`:

```
bin\win10\
  EchoXR.exe                  the launcher
  EchoXR\                     OpenXR runtime, loader, licences, README.txt
```

Run `EchoXR.exe` with SteamVR installed. On each launch it sets up whatever is
missing or out of date, then starts Echo:

- **`echovr_openxr.exe`:** a patched copy of `echovr.exe`, made on the player's
  machine. No game file ships in the zip.
- **`EchoXR\echoxr.ini`:** created on first launch. The zip doesn't ship one, so
  unzipping a newer release never resets your choices.
- **Hand tracking**, when it's switched on: see [Hand tracking](#hand-tracking).

`EchoXR.exe --setup-only` does the setup without starting Echo.

### Installer

`EchoXRSetup.exe` finds Echo on its own, or you can browse to it. It installs
EchoXR, hand tracking, or both, and shows each part as a switch:

| switch | what it installs |
| --- | --- |
| Hand tracking | `plugins\EchoXRHands.dll`, `plugins\EchoXRHands.txt` and the settings window `EchoXR\Hands\EchoXRSettings.exe`. An existing `EchoXRHands.txt` is kept; new defaults go to `EchoXRHands.default.txt`. |
| Finger bridge | `EchoXR\Hands\`: `EchoXRHands.exe`, its SteamVR manifest, `openvr_api.dll`, `fake_index.py`, `version.txt` |
| EchoXR runtime | `EchoXR.exe`, `EchoXR\` (runtime, OpenXR loader, licences) and the patched `echovr_openxr.exe` |
| Start hand tracking with EchoXR | `EchoXR\echoxr.ini` `AutoStartHands = 1` or `0` |
| Open settings with EchoXR | `EchoXR\echoxr.ini` `AutoStartSettings = 1` or `0` (off by default) |
| Plugin loader | `dbgcore.dll`, following the [loader rules](https://github.com/heisthecat31/EchoXR-Hands#plugin-loader) |
| Desktop shortcuts | `EchoXR.lnk`, `EchoXR Hands.lnk` and `EchoXR Hands Settings.lnk` |

The installer carries the hand tracking release from when it was built.
`EchoXR.exe` then updates hand tracking from the EchoXR Hands releases like any
other install.

The installer also clears out pre-rename files. It removes
`plugins\HandTrackingValve.dll` and the `HandTrackingBridge\` folder, and renames
`handtracking_config.txt` to `EchoXRHands.txt`. Old desktop shortcuts into that
install are replaced with the new ones. It warns if Echo or the bridge is running,
and offers to rerun as administrator if Windows blocks the folder.

Uninstall removes the switched-on parts. It keeps `EchoXRHands.txt`, and it keeps
the loader unless the previous `dbgcore.dll` can be put back.

To run it without the window:

```
EchoXRSetup.exe --silent [--dir <folder>] [--components <mask>] [--uninstall]
```

The mask bits are 1 hand tracking, 2 bridge, 4 EchoXR, 8 loader, 16 shortcuts,
32 auto-start hand tracking and 64 auto-open settings. The log is `%TEMP%\EchoXRSetup.log`.

### Hand tracking

EchoXR Hands is released on its own, at
[heisthecat31/EchoXR-Hands](https://github.com/heisthecat31/EchoXR-Hands/releases).
`AutoStartHands` in `EchoXR\echoxr.ini` decides whether `EchoXR.exe` uses it:

- **`AutoStartHands = 1`, hand tracking not installed:** `EchoXR.exe` downloads
  the latest `EchoXR-Hands-v*.zip`, unpacks it into `EchoXR\Hands\`, and installs
  the plugin and plugin loader ([loader rules](https://github.com/heisthecat31/EchoXR-Hands#plugin-loader)).
- **`AutoStartHands = 1`, installed:** at most once a day it compares the latest
  release with `EchoXR\Hands\version.txt`, and asks before installing a newer one.
  It skips the check while the bridge or the settings window is running.
- **Every launch with `AutoStartHands = 1`:** the finger bridge runs minimised
  alongside Echo, is restarted if it drops out, and is closed when Echo exits.

The first launch writes `AutoStartHands = 1` when hand tracking is already there
(from the installer, or an EchoXR Hands zip), and `0` otherwise. Using hand
tracking itself is covered in the [EchoXR Hands README](https://github.com/heisthecat31/EchoXR-Hands).

### Linux (untested)

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

Under Wine, `EchoXR.exe` can't download hand tracking (there's no `tar.exe`):
unzip `EchoXR-Hands-v*.zip` into `bin/win10` yourself.

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
- **Keeps the plugin loader:** `WINEDLLOVERRIDES=dbgcore=n,b`, because Wine
  would otherwise load its own `dbgcore.dll` instead of the plugin loader.

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
- whether the finger bridge (`EchoXRHands.exe`, OpenVR) works. It should on
  SteamVR. On Monado or WiVRn it depends on xrizer or OpenComposite passing hand
  skeletons through.

## Using it

1. Start SteamVR.
2. Run `EchoXR.exe` (or the EchoXR desktop shortcut).

`EchoXR\echoxr.ini` holds the launcher's settings:

| setting | what it does |
| --- | --- |
| `AutoStartHands` | 1 = run the hand tracking bridge alongside Echo, and download EchoXR Hands first if it isn't installed |
| `AutoStartSettings` | 1 = open the hand tracking settings window alongside Echo, and close it when Echo exits |
| `CheckForUpdates` | 1 (default) = check GitHub for EchoXR and, with hand tracking on, EchoXR Hands updates |

With `CheckForUpdates = 1`, `EchoXR.exe` asks GitHub for the latest release at
most once every 20 hours, with a 4-second timeout so a launch is never held up.
If there's a newer `EchoXR-v*.zip`, it asks first, then downloads it, checks it,
unpacks it over the install and restarts itself. Hand tracking updates work the
same way, from the EchoXR Hands releases. `EchoXR.exe --check-update` checks both
straight away.

## Logs

| log | what's in it |
| --- | --- |
| `EchoXR\launcher.log` | first-run setup, updates, hand tracking downloads, the runtime chosen, the bridge starting and stopping, Echo's exit code |
| `EchoXR\runtime.log` | the OpenXR runtime and its extensions, the controller profile bound to each hand, and every failed OpenXR call |
| `%TEMP%\EchoXRSetup.log` | what the installer wrote, kept, moved or removed |

Hand tracking's own log is `plugins\EchoXRHands.log` ([EchoXR Hands](https://github.com/heisthecat31/EchoXR-Hands#logs)).

## Building

Everything builds with MSVC (Visual Studio 2026 toolset).

| command | builds |
| --- | --- |
| `xr\build_xr.bat` | `xr\out\LibOVRRT64_1.dll`, `openxr_loader.dll` and `EchoXR.exe`. It needs three upstream checkouts plus a patch; [xr/README.md](xr/README.md) has the exact commits and commands |
| `xr\build_launcher.bat` | just `xr\out\EchoXR.exe` (quick) |
| `installer\build_installer.bat [--all]` | all of the above as needed, plus hand tracking from the EchoXR-Hands checkout, then `out\EchoXRSetup.exe` |
| `python tools\make_release.py [--no-build]` | `out\release\EchoXR-v<VERSION>.zip` and `EchoXRSetup-v<VERSION>.exe`, for this repository's releases |
| `python tools\gen_logo.py` | the logo in `installer\logo\` (SVG, PNG, ICO) |

The plugin loader is staged from the game install into
`installer\stage\dbgcore.dll` when the installer is built. The version number is
in `VERSION`.

The installer bundles hand tracking, so building it needs
[EchoXR Hands](https://github.com/heisthecat31/EchoXR-Hands) checked out next to this repository, as a folder named
`EchoXR-Hands`. `build_installer.bat` runs its `build.bat` and takes the files from
its `out\` folder, and its `VERSION` becomes the installed `EchoXR\Hands\version.txt`.
Hand tracking releases are built and published from that repository.
`linux\echoxr-linux.sh` needs no build; the release zip ships it as
`EchoXR/echoxr-linux.sh`.

| folder | what |
| --- | --- |
| `xr/` | EchoXR runtime glue, launcher and updater (`src/`), Revive patch (`patches/`) |
| `third_party/` | Detours, for the runtime's D3D hooks |
| `installer/` | `EchoXRSetup.exe` source, logo |
| `linux/` | Linux launcher script |
| `tools/` | release packaging, logo generator |
