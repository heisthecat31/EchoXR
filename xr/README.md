# EchoXR — Echo VR through OpenXR

EchoXR runs Echo VR with no Oculus runtime in the path. Echo was written against
Oculus's LibOVR API. EchoXR answers those calls with an OpenXR implementation, so the
game renders and tracks through SteamVR's OpenXR runtime on Windows, through Proton's
wineopenxr on Linux (SteamVR, Monado, WiVRn), or any other OpenXR runtime you choose.

It ships as the `EchoXR-OpenXR-v<version>.zip` release (see the
[README](../README.md)).

---

## The patched game executable

Echo's built-in Oculus loader only accepts an Oculus-signed runtime, and the
EchoXR runtime isn't signed by Oculus. EchoXR therefore runs Echo from
`echovr_openxr.exe`, a copy of `echovr.exe` in which the LibOVR runtime signature
check always passes. The function at RVA `0x1365bd0` starts with
`mov eax,1 ; ret` (6 bytes) instead of its prologue. `echovr.exe` itself is
never changed, so normal launches are unaffected.

The copy is made on the player's own machine, by `EchoXR.exe` on its first
launch. It uses `src/echoxr_common.h`, which first checks the 14 prologue bytes
(`48 89 5C 24 18 48 89 74 24 20 55 57 41 56`); on any other game build it refuses
and leaves nothing behind. No game binary ships with this project. Uninstalling
EchoXR deletes the copy.

---

## What each part is

| File | Where it goes | What it does |
| --- | --- | --- |
| `LibOVRRT64_1.dll` | `bin\win10\EchoXR\` | The runtime. Revive's OpenXR backend ("ReviveXR", MIT), built under the Oculus runtime's DLL name. It implements every one of the 93 `ovr_*` functions Echo uses (125 in total) on top of OpenXR. |
| `openxr_loader.dll` | `bin\win10\EchoXR\` | The official Khronos OpenXR loader, built from source (OpenXR SDK 1.1.63). |
| `LibOVRPlatform64_1.dll` | `bin\win10\EchoXR\` | The Oculus Platform SDK, answered without Meta (`platform/`, by marshmallow-mia). The game's `pnsovr.dll` signs in through it: a signed-in user with a per-machine id (salted SHA-256 of the NIC's MAC under Wine, of Windows' MachineGuid otherwise; the Steam name as display name when there is one), entitled, and the microphone through WASAPI. Meta's loader needs the Oculus service and an Oculus account. |
| `EchoXR.exe` | `bin\win10\` | The launcher, described below. |
| `echovr_openxr.exe` | `bin\win10\` | The patched copy of `echovr.exe`, made on your machine by the launcher (see above). |

The runtime lives in its own `EchoXR\` subfolder on purpose. Echo searches its
own folder first for its runtime, so a runtime placed there would take over
normal launches too. Kept in the subfolder, it's only used when the launcher
points Echo at it. Starting `echovr.exe` the usual way is unaffected.

### The launcher, `EchoXR.exe`

Source: `src/launcher.cpp`, with `src/proton_vr.h` and `src/preflight.h`. It has no
window: it sets up one launch, starts Echo, waits for it to exit and returns Echo's exit
code, or one of its own (listed in [docs/ADVANCED.md](../docs/ADVANCED.md)).

1. **Checks and sets up.** `EchoXR.exe` has to be next to `echovr.exe`, and
   `EchoXR\LibOVRRT64_1.dll` and `openxr_loader.dll` have to exist. If
   `echovr_openxr.exe` is missing, it makes it (see above). `--setup-only` stops here.
2. **Chooses the VR runtime.** It finds SteamVR through Steam's own registry
   (`%LOCALAPPDATA%\openvr\openvrpaths.vrpath`). It then points this launch at
   SteamVR's `steamxr_win64.json` via the loader's `XR_RUNTIME_JSON` variable.
   This matters because some apps (the Virtual Desktop streamer, for one) keep
   making themselves the system-wide OpenXR runtime. `--runtime active` uses the
   system runtime instead. Under Wine/Proton (it checks for `wine_get_version`) it
   leaves the choice to Proton, whose registered runtime is `wineopenxr`.
3. **Under Proton: makes sure OpenXR is on** (`src/proton_vr.h`). Proton's own VR
   setup only gets to OpenXR after it has started an OpenVR client. When it didn't
   (no OpenVR runtime, VR service not up yet), `EchoXR.exe` calls the same
   `wineopenxr_init_registry` export Proton calls, with a 20 s limit, and marks the
   prefix's volatile `HKCU\Software\Wine\VR` key ready. See "Linux in detail" in
   [docs/ADVANCED.md](../docs/ADVANCED.md).
4. **Checks the runtime and the headset** (`src/preflight.h`). Through
   `EchoXR\openxr_loader.dll` it creates an OpenXR instance (20 s limit) and asks for a
   head-mounted system, waiting up to 15 s for one that's asleep. The runtime's and the
   headset's names go into the log.
5. **Tells Echo a headset is present.** Echo checks for the named Windows event
   `OculusHMDConnected` before it starts VR; normally the Oculus service creates
   it. The launcher creates it when nothing else has.
6. **Points Echo at the runtime folder.** It sets `LIBOVR_DLL_DIR` to
   `bin\win10\EchoXR\`, the first place Echo's loader looks. It also adds that
   folder to `PATH` so `openxr_loader.dll` is found.
7. **Starts Echo.** That's `echovr_openxr.exe` by default, or `--exe <name>` for
   another executable in the same folder. Any other arguments are passed to Echo.
   It waits until Echo exits.

The environment settings apply only to the Echo process the launcher starts;
nothing system-wide is changed. It logs to `bin\win10\EchoXR\launcher.log`.

---

## Setting up a PC

1. **Install SteamVR** (free, in Steam) and check that your headset shows up in
   it. It doesn't have to be set as the system OpenXR runtime; the launcher picks
   SteamVR for each launch on its own.
2. **Unzip the release** into the Echo install's `bin\win10` folder, so that
   `EchoXR.exe` sits next to `echovr.exe`.
3. **Run** `bin\win10\EchoXR.exe` with SteamVR running.

The Oculus login/platform side is separate from all of this. Echo's online
features on the community servers use the community platform files, the same as
with any other launch.

---

## Building

See "Building" in [docs/ADVANCED.md](../docs/ADVANCED.md): one CMake build, with MSVC on
Windows or clang-cl on macOS and Linux. Its inputs:

| What | Where | Pinned at |
| --- | --- | --- |
| Revive's OpenXR backend (MIT), with EchoXR's changes | `xr/revive/`, in this repository | upstream `ab73167` |
| Oculus PC SDK **1.55** headers and two shim sources (build time only) | github.com/sparsebase/ovr_sdk_win | `4b1d6b7` |
| OpenXR SDK 1.1.63 and its loader | github.com/KhronosGroup/OpenXR-SDK | `f2448a8` |
| Vulkan headers, microprofile (Revive's externals) | KhronosGroup/Vulkan-Headers, zeux/microprofile | Revive's submodule commits |
| Microsoft Detours (MIT) | github.com/microsoft/Detours | `1dbd4ea` |

The Oculus SDK has to be **1.55 or newer**. Echo asks for SDK minor version 55,
and 1.43 is missing `ovrHmdColorDesc` and the separate audio-device error codes
that Revive uses.

The runtime is ReviveXR's sources, with Revive's own `main.cpp` swapped for
`src/xr_main.cpp`, built as `LibOVRRT64_1.dll`. The launcher is `src/launcher.cpp`.

### What `src/xr_main.cpp` replaces

Revive normally runs as an injection layer: its `main.cpp` hooks `LoadLibrary`
so a game asking for the Oculus runtime gets Revive instead. EchoXR doesn't do
that. The runtime is loaded under the Oculus runtime's own name, from a folder
the launcher points Echo at. So `xr_main.cpp` is a plain `DllMain`. It also
writes `EchoXR\runtime.log` (see below).

---

## Changes made to Revive for SteamVR

Revive had mostly been used with Oculus-style runtimes. SteamVR rejected three
things. Each was found from `runtime.log`, which names the exact OpenXR call that
failed, and fixed in this copy of Revive (`xr/revive/`):

1. **OpenXR version.** Built against the 1.1 SDK, Revive asked for OpenXR 1.1.
   The runtime refused (`XR_ERROR_API_VERSION_UNSUPPORTED`), which Echo reported
   as "Failed to initialize Oculus VR session" (`-3004`). It now asks for
   OpenXR 1.0 (`Runtime.cpp`).
2. **A struct from an extension that wasn't enabled.** Revive always attached an
   `XR_EPIC_view_configuration_fov` struct when reading view configurations.
   SteamVR doesn't offer that extension and validates strictly, so the call
   failed with `XR_ERROR_VALIDATION_FAILURE`. The struct is now only attached
   when the extension is enabled (`Session.cpp`).
3. **Reading the field of view too early.** SteamVR only answers `xrLocateViews`
   once a session is running. SteamVR now gets the same "wait until the session
   is ready" handling Revive already used for Windows Mixed Reality, with a
   5-second limit so an asleep headset can't hang Echo's startup. Revive's
   Echo-specific setting now also applies to the `echovr_openxr.exe` filename
   (`Runtime.cpp`, `Session.cpp`).

One more change wasn't a failed call:

4. **Controllers that didn't track (Steam Frame).** On SteamVR, Revive suggested
   controller bindings for the Valve Index profile only, and left SteamVR to
   convert them for every other controller. SteamVR doesn't convert them for the
   Steam Frame's controllers, so those had no binding: no pose, no buttons, and
   Echo saw them as not tracked. Revive now also suggests the Oculus Touch
   profile, which nearly every OpenXR game uses and new controllers support, and
   SteamVR picks whichever fits the controllers best. Index controllers still
   use the Index profile (`InputManager.cpp`). The profile SteamVR chose for each
   hand goes into `runtime.log`; `none` means that hand has no binding.

Result on SteamVR/OpenXR 2.17.10: Echo runs, with **no** failed OpenXR calls for
the whole session.

---

## Changes in 0.4.2 (no Meta at all)

- **The Platform SDK (`platform/`).** Echo's `pnsovr.dll` signs in through Oculus'
  Platform SDK, which EchoXR's runtime didn't cover: Meta's loader had to come from a
  Windows PC or Meta's runtime package, needed the Oculus service (none under Proton), and
  stopped Echo with "Failed to initialize the Oculus Platform SDK". EchoXR now brings its
  own `EchoXR\LibOVRPlatform64_1.dll` (marshmallow-mia's, from NoOvrEchoVR_on_Linux):
  `EchoXR.exe` already puts `EchoXR\` first on `PATH` and in `LIBOVR_DLL_DIR`, so the game
  loads it there, and only when EchoXR starts it. It logs to `EchoXR\platform.log`.

## Changes in 0.4.1 (GE-Proton)

Tested with a Quest 3 on SteamVR 2.16.7 (Steam Link) and on WiVRn 26.9, under GE-Proton11-3:

- **OpenXR's set-up (`EchoXR.exe`).** GE-Proton's wineopenxr writes OpenXR's Vulkan
  extensions into `HKCU\Software\Wine\XR`, not `Wine\VR`. 0.4.0 looked only in `Wine\VR`
  and stopped with exit code 5 although the runtime answered; 0.4.1 takes them from
  `Wine\XR` and copies them into `Wine\VR` (`proton_vr.h`).
- **The first swapchain image (`Swapchain.cpp`).** wineopenxr (D3D12) knows a swapchain's
  image count only after its images are enumerated, and refuses an acquire before that
  (`XR_ERROR_CALL_ORDER_INVALID`): Echo stopped with "Unknown error while loading the
  game". The count is asked for before the first acquire now.

## Changes for Proton, and for every runtime (0.4.0)

Comparing this runtime with [RiftLift](https://github.com/Villagers654/RiftLift)'s (also
derived from Revive) showed where Revive's 2023 backend would break under Proton and on
other runtimes. Each fix below is EchoXR's own code; RiftLift (GPL-3.0) is credited for
the findings. The decisions that can be tested away from a headset live in
`src/echoxr_policy.h`, with tests in `tests/`.

1. **One graphics API under Proton.** Proton's wineopenxr turns each D3D11, D3D12 and
   Vulkan request into `XR_KHR_vulkan_enable`, so Revive's request named it three
   times, which SteamVR refuses. Under Wine only D3D12 is enabled, the API Echo renders
   with (its `echovr.exe` builds a D3D12 device), and the adapter comes from the D3D12
   requirements (`Runtime.cpp`, `Session.cpp`). A D3D11 game gets a clear error there.
2. **The field-of-view probe** (`Session.cpp`, `ProbeViews`). Echo asks for its render
   sizes before it hands over its device, so a short-lived session reads the field of
   view first. It now uses no device at all where the runtime offers
   `XR_MND_headless`. Otherwise it uses a temporary device of the game's own API (D3D12
   under Proton, so wineopenxr never sees two graphics APIs on one instance), on the
   runtime's adapter or else the first one (Revive passed a null adapter). It waits for
   READY on every runtime, for at most 10 s, and then fails with a log line instead of
   reading views from a session that isn't running.
3. **The first frame.** Revive began a frame on whichever thread polled the session
   status, which races the game's own frame calls on its render thread. Now the
   eye-level origin is recentered after the game's first `xrWaitFrame`, and a game that
   only calls `ovr_SubmitFrame` gets its first frame opened there (`Session.cpp`,
   `REV_CAPI.cpp`).
4. **Swapchain formats.** D3D12 swapchains were created with the game's format even when
   the runtime didn't offer it (Revive negotiated a format, then didn't use it). D24S8
   depth becomes D32S8 when the runtime lacks it, as on AMD GPUs through Proton
   (`Swapchain.cpp`, `SwapchainD3D12.cpp`).
5. **Opaque eye layers**, as on Oculus's compositor; quads, cylinders and cubes keep
   their alpha (`REV_CAPI.cpp`).
6. **Input.** Reading input or tracking syncs the actions again when the last sync is
   more than 5 ms old, so controllers aren't untracked before the first frame, and one
   lock covers every action call, since Echo reads from several threads
   (`InputManager.cpp`).
7. **Audio.** The microphone query answered with the speakers; endpoint properties are
   read safely and freed (`REV_CAPI_Audio.cpp`).
8. **Results and smaller bugs.** Starting a session is checked on every graphics path,
   `ovr_Create` cleans up after a failed setup, `ovr_Destroy` accepts null, the tracking
   caps report Position, and missing `return`s in the Vulkan and audio paths are back.

---

## Logs

| Log | What's in it |
| --- | --- |
| `bin\win10\EchoXR\launcher.log` | every launch (dated, kept up to about 1 MB): which runtime was chosen, Proton's OpenXR setup, the runtime and headset found, what was launched, Echo's exit code |
| `bin\win10\EchoXR\platform.log` | the Platform SDK stand-in: the identity's source and ids, and every call `pnsovr.dll` made |
| `bin\win10\EchoXR\runtime.log` | the latest launch: Windows or Wine, the OpenXR runtime's name and version, enabled extensions (and any required one missing), the SDK version Echo asked for, the field-of-view probe, session states, swapchain format changes, the first submitted frame, the controller profile bound to each hand, and **every failed OpenXR call** with its source line |
| `_local\r14logs\*.log` | Echo's own log ("Initializing OVR session…" and any session error) |
| `Steam\logs\vrserver.txt` | SteamVR's side of the connection |

---

## Credits and licences

- **Platform SDK stand-in** (`platform/`) — marshmallow-mia, first written for
  NoOvrEchoVR_on_Linux.
- **Revive / ReviveXR** — LibreVR, MIT License.
- **OpenXR SDK and loader** — The Khronos Group, Apache 2.0.
- **Oculus PC SDK headers** — Oculus/Meta. Used at build time only.
- **Microsoft Detours** — MIT License. Used by ReviveXR's D3D code.
- **RiftLift** — Villagers654, GPL-3.0. No code is used; its findings shaped the 0.4.0
  fixes above.
