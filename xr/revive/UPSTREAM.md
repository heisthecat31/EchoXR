# Revive, as EchoXR uses it

These are the parts of [Revive](https://github.com/LibreVR/Revive) (MIT, see `LICENSE`) that
EchoXR builds into `LibOVRRT64_1.dll`: the OpenXR backend `ReviveXR`, its version header and the
glad OpenGL loader. They come from upstream commit `ab73167e2380135aee9fe9f68874ec7dd2912cb0`
(2023-08-29), the latest one.

Left out on purpose:

- `ReviveXR/main.cpp`: Revive's injection glue. EchoXR loads the runtime under the Oculus
  runtime's name instead, and `xr/src/xr_main.cpp` is its entry point.
- The Visual Studio project, resource and `.def` files: CMake builds it (see the top-level
  `CMakeLists.txt`), and the `ovr_*` exports come from `OVR_PUBLIC_FUNCTION`.
- Revive's other components (the injector, the OpenVR backend, the overlay).

Everything EchoXR changed since upstream is in this repository's history, starting from the
commit that added these files unchanged.
