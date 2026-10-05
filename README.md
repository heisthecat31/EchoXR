<img src="logo/echoxr.png" width="96" alt="EchoXR logo">

# EchoXR

EchoXR runs **Echo VR through OpenXR**, with no Oculus or Meta app: on Windows on
SteamVR, and on Linux through Proton on SteamVR, Monado or WiVRn. It answers Echo's
Oculus calls with Revive's OpenXR backend. There's no UI; it writes log files.

The [Echo VR launcher](https://github.com/EchoVRCE/Echo-VR-Installer) installs and starts
EchoXR for you. To set it up by hand, take the latest `EchoXR-OpenXR-v<version>.zip` from
[Releases](https://github.com/EchoTools/EchoXR/releases).

## Windows

1. Install SteamVR and check that your headset works in it.
2. Unzip the release into Echo's `bin\win10` folder, next to `echovr.exe`.
3. Run `bin\win10\EchoXR.exe`.

## Linux

1. Unzip the release into Echo's `bin/win10` folder. Nothing of Meta's is needed: EchoXR
   brings its own `LibOVRPlatform64_1.dll` (no Oculus service, no Oculus account). Remove a
   `LibOVRPlatform64_1.dll` from `bin/win10` itself if one is there; it would be used instead.
2. Start SteamVR, `monado-service` or `wivrn-server`, and wake the headset.
3. Run `bin/win10/EchoXR/echoxr-linux.sh --check`, then without `--check`.

It needs Steam with Proton 9 or newer (or GE-Proton) and an active OpenXR runtime. No
OpenVR runtime (xrizer, OpenComposite) is needed.

## When Echo doesn't start

Look in `bin/win10/EchoXR/launcher.log` and `runtime.log`. EchoXR's exit codes:

| code | meaning |
| --- | --- |
| 5 | no OpenXR runtime answered: start SteamVR, Monado or WiVRn first |
| 6 | no headset: connect it and wake it up |
| 2, 3, 4, 7 | install problem: see the log |

Only the current live build of Echo is supported.

## More

[docs/ADVANCED.md](docs/ADVANCED.md) has the details: how it works, Linux under the hood,
every log and exit code, and building. [xr/README.md](xr/README.md) explains the runtime.

Credits: EchoXR by heisthecat31 (this fork keeps only its OpenXR layer); Revive by LibreVR
(MIT); the OpenXR SDK by Khronos (Apache 2.0); Microsoft Detours (MIT). Findings from
[RiftLift](https://github.com/Villagers654/RiftLift) shaped the 0.4.0 fixes.
