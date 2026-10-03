# EchoXR: Echo VR's LibOVR calls answered over OpenXR.
#
# Builds, into <build>/out:
#   LibOVRRT64_1.dll   the runtime: Revive's OpenXR backend (xr/revive) under the Oculus
#                      runtime's name, with xr/src/xr_main.cpp as its entry point
#   openxr_loader.dll  the Khronos OpenXR loader
#   EchoXR.exe         the launcher (xr/src/launcher.cpp)
#
# Everything is linked against the static C runtime, so players need no Visual C++
# redistributable. Presets: windows-msvc (MSVC on Windows) and cross-clang-cl (clang-cl and
# Microsoft's SDK from xwin, on macOS or Linux). See README.md "Building".
cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
cmake_policy(SET CMP0135 NEW)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ECHOXR_VERSION)
string(STRIP "${ECHOXR_VERSION}" ECHOXR_VERSION)

project(EchoXR VERSION ${ECHOXR_VERSION} LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "EchoXR builds for 64-bit Windows only (use a preset).")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(OUT_DIR "${CMAKE_BINARY_DIR}/out")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUT_DIR}")

# --- Pinned inputs, fetched as source -----------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# The Khronos OpenXR SDK 1.1.63: headers and the loader.
FetchContent_Declare(openxr_sdk
    URL https://github.com/KhronosGroup/OpenXR-SDK/archive/f2448a8797c85814aa892efc1ab8707900fbcc78.tar.gz
    URL_HASH SHA256=1681e0f6c9633c73117c2fc376d1fe71cbd6fdd2aeb896ed5854ff022aa82eaa)
# The Oculus PC SDK 1.55: headers and the two shim sources the runtime compiles in.
# Build-time only; nothing of it is shipped.
FetchContent_Declare(ovr_sdk
    URL https://github.com/sparsebase/ovr_sdk_win/archive/4b1d6b7e86077062e5de33dda7d77b5790202c4c.tar.gz
    URL_HASH SHA256=b5febe29430080e901128e5de85ec4d16b8115c47f8a2953763bec378263e8c8)
# Revive's externals, at the commits Revive ab73167 pins.
FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/d732b2de303ce505169011d438178191136bfb00.tar.gz
    URL_HASH SHA256=570f9ae1e65466dbaf5fcab667abd079dd0a61c4ab86cf535efd492bf70a5b74)
FetchContent_Declare(microprofile
    URL https://github.com/zeux/microprofile/archive/deda3d42c56e5d816daa480b3cff066e76b6b90b.tar.gz
    URL_HASH SHA256=0ecbb5bb07dc54b559ece8d5664cc8efb4417388429c9c3202d72030702eec50)
# Microsoft Detours (MIT): ReviveXR hooks two D3D device methods with it.
FetchContent_Declare(detours
    URL https://github.com/microsoft/Detours/archive/1dbd4ea281d73d478aa8d09a8b83fbd265f715f7.tar.gz
    URL_HASH SHA256=38d435d2750b4e3af57f56c77af52bc058123c5e7dd734945b7680ec52e5c91c)

FetchContent_MakeAvailable(ovr_sdk vulkan_headers microprofile detours)

# The loader as a DLL; nothing else of the SDK (it uses its pre-generated sources, so no
# Python is needed).
set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_SYSTEM_JSONCPP OFF CACHE BOOL "" FORCE)
set(INSTALL_SYMBOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(openxr_sdk)

# --- Detours ------------------------------------------------------------------------------
set(DETOURS_SRC "${detours_SOURCE_DIR}/src")
add_library(detours STATIC
    ${DETOURS_SRC}/detours.cpp ${DETOURS_SRC}/modules.cpp ${DETOURS_SRC}/disasm.cpp
    ${DETOURS_SRC}/image.cpp ${DETOURS_SRC}/creatwth.cpp ${DETOURS_SRC}/disolx86.cpp
    ${DETOURS_SRC}/disolx64.cpp ${DETOURS_SRC}/disolia64.cpp ${DETOURS_SRC}/disolarm.cpp
    ${DETOURS_SRC}/disolarm64.cpp)
target_compile_definitions(detours PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 _AMD64_)
# Revive includes it as <detours/detours.h>.
set(DETOURS_INCLUDE "${CMAKE_BINARY_DIR}/detours-include")
file(COPY ${DETOURS_SRC}/detours.h ${DETOURS_SRC}/detver.h DESTINATION "${DETOURS_INCLUDE}/detours")
target_include_directories(detours PUBLIC "${DETOURS_INCLUDE}")

# --- The runtime, LibOVRRT64_1.dll --------------------------------------------------------
set(REVIVE "${CMAKE_CURRENT_SOURCE_DIR}/xr/revive")
add_library(echoxr_runtime SHARED
    ${REVIVE}/ReviveXR/Common.cpp
    ${REVIVE}/ReviveXR/HapticsBuffer.cpp
    ${REVIVE}/ReviveXR/InputManager.cpp
    ${REVIVE}/ReviveXR/REV_CAPI.cpp
    ${REVIVE}/ReviveXR/REV_CAPI_Audio.cpp
    ${REVIVE}/ReviveXR/REV_CAPI_D3D.cpp
    ${REVIVE}/ReviveXR/REV_CAPI_GL.cpp
    ${REVIVE}/ReviveXR/REV_CAPI_Vk.cpp
    ${REVIVE}/ReviveXR/Session.cpp
    ${REVIVE}/ReviveXR/Runtime.cpp
    ${REVIVE}/ReviveXR/Swapchain.cpp
    ${REVIVE}/ReviveXR/SwapchainD3D11.cpp
    ${REVIVE}/ReviveXR/SwapchainD3D12.cpp
    ${REVIVE}/ReviveXR/SwapchainGL.cpp
    ${REVIVE}/ReviveXR/SwapchainVk.cpp
    ${REVIVE}/ReviveXR/microprofile.cpp
    ${REVIVE}/Externals/glad/src/glad.c
    ${ovr_sdk_SOURCE_DIR}/LibOVR/Shim/OVR_CAPI_Util.cpp
    ${ovr_sdk_SOURCE_DIR}/LibOVR/Shim/OVR_StereoProjection.cpp
    xr/src/xr_main.cpp)
set_target_properties(echoxr_runtime PROPERTIES OUTPUT_NAME LibOVRRT64_1 PREFIX "")
# ReviveXR's own folder stays off the include path: its OVR_CAPI.h wraps the SDK's
# <OVR_CAPI.h>, and quoted includes find the folder's files anyway.
target_include_directories(echoxr_runtime PRIVATE
    ${REVIVE}/include
    ${REVIVE}/Externals/glad/include
    ${ovr_sdk_SOURCE_DIR}/LibOVR/Include
    ${vulkan_headers_SOURCE_DIR}/include
    ${microprofile_SOURCE_DIR})
target_compile_definitions(echoxr_runtime PRIVATE
    XR_USE_PLATFORM_WIN32 VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR NOMINMAX
    MICROPROFILE_ENABLED=0 MICROPROFILE_GPU_TIMERS=0 OVR_DLL_BUILD NDEBUG
    _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
# Revive's sources use std::chrono without including it everywhere.
target_compile_options(echoxr_runtime PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/EHsc /FIchrono>)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Revive is MSVC-first: keep clang-cl to what MSVC itself would report.
    target_compile_options(echoxr_runtime PRIVATE
        -Wno-microsoft -Wno-unused-parameter -Wno-missing-field-initializers
        -Wno-unused-variable -Wno-sign-compare -Wno-switch -Wno-deprecated-declarations
        -Wno-unused-function -Wno-nonportable-include-path -Wno-pragma-pack
        -Wno-unknown-pragmas -Wno-reorder-ctor)
    target_compile_options(detours PRIVATE -Wno-microsoft -Wno-unused-parameter)
endif()
target_link_libraries(echoxr_runtime PRIVATE
    openxr_loader detours
    ws2_32 opengl32 d3d11 d3d12 dxgi dxguid dsound winmm shlwapi pathcch user32 advapi32 ole32)

# --- The launcher, EchoXR.exe -------------------------------------------------------------
add_executable(EchoXR xr/src/launcher.cpp xr/src/launcher.rc)
target_compile_definitions(EchoXR PRIVATE UNICODE _UNICODE ECHOXR_VERSION="${ECHOXR_VERSION}")
target_compile_options(EchoXR PRIVATE /EHsc)
# No console window: everything EchoXR.exe has to say goes to EchoXR\launcher.log.
target_link_options(EchoXR PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup)
target_include_directories(EchoXR PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_link_libraries(EchoXR PRIVATE advapi32 shell32 user32)

# --- Tests (run on Windows, or under Wine) ------------------------------------------------
enable_testing()
add_subdirectory(tests)
