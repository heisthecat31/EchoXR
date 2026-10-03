# EchoXR in detail

The [README](../README.md) covers installing and running EchoXR. This page has the rest:
what it does exactly, Linux under the hood, logs and exit codes, and building it. How the
runtime itself works, and what was changed in Revive, is in [xr/README.md](../xr/README.md).

## What it does

| | |
| --- | --- |
| **Echo on OpenXR** | `EchoXR.exe` starts Echo with the EchoXR runtime: Revive's OpenXR backend under the Oculus runtime's DLL name, and the Khronos OpenXR loader. No Visual C++ runtime needed. |
| **No game file changed** | It runs `echovr_openxr.exe`, a copy of `echovr.exe` made on the player's machine whose runtime signature check passes. `echovr.exe` itself is never touched. |
| **SteamVR per launch** | On Windows each launch is pinned to SteamVR's OpenXR runtime, even when another app made itself the system runtime. `--runtime active` uses the system runtime instead. |
| **Linux without OpenVR** | Under Proton, `EchoXR.exe` sets Proton's OpenXR up itself when Proton didn't, so Monado and WiVRn need no xrizer or OpenComposite. |
| **Checks first** | Before Echo starts, `EchoXR.exe` checks that the OpenXR runtime answers and has a headset (waiting a little for one that's asleep). Problems end with a log line and an exit code, not with Echo starting without VR. |

### Limits

- **One game build.** The patch for `echovr_openxr.exe` is for the current `echovr.exe`
  (35,397,120 bytes, May 2023). It checks the bytes it changes first, and refuses
  anything else instead of breaking the game.
- **D3D12 under Proton.** Under Wine the runtime enables one graphics API, D3D12, which
  Echo uses (see xr/README.md for why).

## Launcher arguments

`EchoXR.exe` has no window and no settings. It passes every argument it doesn't know on
to Echo; its own are `--runtime steamvr|active`, `--exe <name>` (another executable in
`bin\win10`) and `--setup-only` (prepare `echovr_openxr.exe`, don't start Echo).

## Linux in detail

The VR path stays inside the game process:

```
echovr_openxr.exe -> EchoXR\LibOVRRT64_1.dll -> EchoXR\openxr_loader.dll
  -> Proton's wineopenxr.dll (the prefix's registered OpenXR runtime)
  -> wineopenxr.so -> the Linux runtime in XR_RUNTIME_JSON
```

**OpenXR without OpenVR.** Proton's own VR setup (its `steam.exe`) only turns
`wineopenxr` on after it has started an OpenVR client, and writes the result into the
prefix's volatile `HKCU\Software\Wine\VR` key. When that didn't happen (no OpenVR
runtime, or the VR service wasn't up yet), `EchoXR.exe` does the OpenXR part itself. It
calls the same `wineopenxr_init_registry` Proton calls, checks that OpenXR's Vulkan
extensions are there, sets `state = 1`, and starts Echo with `DXVK_NO_VR=1`. It gives up
after 20 s (Linux SteamVR's OpenXR waits forever when SteamVR isn't running).

The script:

- **Picks Proton:** the newest GE-Proton, then Proton Experimental, then the
  newest `Proton N`. It refuses one without `wineopenxr`.
- **Picks the runtime:** `XR_RUNTIME_JSON`, or your active one in
  `~/.config/openxr/1/active_runtime.json`.
- **Checks the VR service:** SteamVR's `vrserver`, or Monado's and WiVRn's sockets in
  `$XDG_RUNTIME_DIR`, and tells `EchoXR.exe` (`ECHOXR_VR_SERVICE=ready`). It warns when
  none is running.
- **Uses its own prefix:** `~/.local/share/echoxr/prefix`.
- **Sets Proton up:** the `STEAM_COMPAT_*` variables and `SteamGameId` (Proton runs its
  VR setup only for games), plus `PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1` so the
  container can see the runtime.
- **Keeps the game's plugin loader:** `WINEDLLOVERRIDES=dbgcore=n,b`, because Wine
  would otherwise load its own `dbgcore.dll` instead of the one in `bin/win10`.

`ECHOXR_PROTON`, `ECHOXR_PREFIX`, `ECHOXR_NO_CONTAINER=1` and `ECHOXR_DEBUG=1`
override the choices. `ECHOXR_DEBUG=1` also writes Proton and OpenXR loader logs
to `~/.local/share/echoxr/logs/`. The script's own output goes to
`~/.local/share/echoxr/echoxr-linux.log`, and when Echo exits it explains EchoXR's exit
code and lists every log to send with a bug report.

Several of the fixes in this version follow findings from
[RiftLift](https://github.com/Villagers654/RiftLift), which runs Rift games on Linux. No
RiftLift code is used (it's GPL-3.0); xr/README.md lists what changed and why.

## Logs and exit codes

| log | what's in it |
| --- | --- |
| `EchoXR\launcher.log` | every launch (dated): the runtime chosen, Proton's OpenXR setup, the runtime and headset found, Echo's exit code. Kept up to about 1 MB |
| `EchoXR\runtime.log` | the latest launch's OpenXR side: platform, runtime and extensions, the field-of-view probe, session states, swapchain format changes, the first frame, and every failed OpenXR call |
| `_local\r14logs\*.log` | Echo's own log ("Initializing OVR session…" and any session error) |

`EchoXR.exe` returns Echo's exit code, or one of its own:

| code | meaning |
| --- | --- |
| 2 | `EchoXR.exe` isn't next to `echovr.exe` in `bin\win10` |
| 3 | `EchoXR\LibOVRRT64_1.dll` or `openxr_loader.dll` is missing |
| 4 | `echovr_openxr.exe` couldn't be made |
| 5 | no OpenXR runtime answered (or, under Proton, its VR service isn't running) |
| 6 | the OpenXR runtime has no headset (not connected, or asleep for more than 15 s) |
| 7 | Echo couldn't be started |

## Building

One CMake build makes all three files, `EchoXR.exe`, `LibOVRRT64_1.dll` and
`openxr_loader.dll`, into `build/<preset>/out`. Everything links the C runtime statically,
so players need no Visual C++ redistributable. Every input outside this repository is
fetched as source at a pinned commit, checked by SHA-256 (see `CMakeLists.txt`).

| where | how |
| --- | --- |
| Windows | `build.bat` (finds Visual Studio, builds, runs the tests, packages). By hand, from an "x64 Native Tools" prompt: `cmake --preset windows-msvc`, `cmake --build --preset windows-msvc`, `ctest --preset windows-msvc` |
| macOS or Linux | clang-cl with Microsoft's SDK from [xwin](https://github.com/Jake-Shadle/xwin): install LLVM, lld, CMake and Ninja, then `cargo install xwin --locked` and `xwin --accept-license splat --output ~/.xwin`. Then `cmake --preset cross-clang-cl` and `cmake --build --preset cross-clang-cl` |
| GitHub Actions | `.github/workflows/build.yml` builds every push with MSVC and keeps the zip as an artifact; a `v*` tag publishes it as a release (a prerelease for `-rc` tags) |

`python tools/make_release.py [--build-dir <dir>]` packages a build into
`<build>/release/EchoXR-OpenXR-v<VERSION>.zip` plus its `.sha256`, and writes the
licence notices of everything inside. `python tools/gen_logo.py` makes the logo in
`logo/`. The version number is in `VERSION`.

| folder | what |
| --- | --- |
| `xr/revive/` | Revive's OpenXR backend (MIT), vendored from upstream `ab73167` with EchoXR's changes (`xr/revive/UPSTREAM.md`) |
| `xr/src/` | the runtime's entry point (`xr_main.cpp`) and the launcher |
| `cmake/` | the clang-cl cross toolchain |
| `tests/` | tests the build runs |
| `linux/` | the Linux launcher script (shipped as `EchoXR/echoxr-linux.sh`) |
| `logo/` | the logo |
| `tools/` | release packaging, logo generator |

## Credits and licences

- **EchoXR** by heisthecat31; this fork keeps only its OpenXR layer.
- **Revive / ReviveXR**: LibreVR, MIT License.
- **OpenXR SDK and loader**: The Khronos Group, Apache 2.0.
- **Oculus PC SDK headers**: Oculus/Meta. Used at build time only.
- **Microsoft Detours**: MIT License.
