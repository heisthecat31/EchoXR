# Credits

EchoXR runs Echo VR on OpenXR: SteamVR without Revive, and VR on Linux (SteamVR, WiVRn,
Monado) through Proton.

## People

- **heisthecat31** (he_is_the_cat): created EchoXR. The OpenXR layer for Echo VR, its
  first releases, the Linux launcher, controller support, and the hand tracking that has
  since moved to [EchoXR Hands](https://github.com/EchoTools/EchoXR-Hands).
- **marshmallow-mia**: the 0.4 line. The OpenXR-only package, the CMake build and CI,
  Proton/GE-Proton support, the runtime fixes, and the Echo VR launcher integration.

## What it builds on

| Project | By | Licence | How it is used |
| --- | --- | --- | --- |
| [Revive / ReviveXR](https://github.com/LibreVR/Revive) | CrossVR (Jules Blok) and contributors | MIT | EchoXR's OpenXR backend, vendored in `xr/revive` with EchoXR's changes |
| [OpenXR SDK and loader](https://github.com/KhronosGroup/OpenXR-SDK) | The Khronos Group | Apache-2.0 | Headers and the loader |
| [Microsoft Detours](https://github.com/microsoft/Detours) | Microsoft | MIT | ReviveXR's D3D hooks |
| Oculus PC SDK headers | Oculus / Meta | Oculus SDK licence | Build time only, not shipped |
| [RiftLift](https://github.com/Villagers654/RiftLift) | Villagers654 | GPL-3.0 | No code; its findings shaped the 0.4.0 fixes |

Echo VR is by Ready at Dawn and Meta, who have nothing to do with EchoXR.
