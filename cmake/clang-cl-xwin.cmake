# Cross-builds EchoXR for 64-bit Windows with clang-cl and lld-link, against Microsoft's CRT
# and Windows SDK as `xwin splat` lays them out (default ~/.xwin; XWIN_DIR overrides).
#
#   brew install llvm lld cmake ninja        (or your distribution's packages)
#   cargo install xwin --locked
#   xwin --accept-license splat --output ~/.xwin
#   cmake --preset cross-clang-cl && cmake --build --preset cross-clang-cl
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(DEFINED ENV{XWIN_DIR})
    set(XWIN_DIR "$ENV{XWIN_DIR}")
else()
    set(XWIN_DIR "$ENV{HOME}/.xwin")
endif()
if(NOT EXISTS "${XWIN_DIR}/crt/include")
    message(FATAL_ERROR "No Microsoft CRT/SDK at ${XWIN_DIR}: run `xwin --accept-license splat --output ${XWIN_DIR}`.")
endif()

set(_llvm_hints /opt/homebrew/opt/llvm/bin /opt/homebrew/opt/lld/bin /usr/local/opt/llvm/bin
    /usr/local/opt/lld/bin /usr/lib/llvm/bin /usr/bin)
find_program(CLANG_CL clang-cl HINTS ${_llvm_hints} REQUIRED)
find_program(LLD_LINK lld-link HINTS ${_llvm_hints} REQUIRED)
find_program(LLVM_LIB llvm-lib HINTS ${_llvm_hints} REQUIRED)
find_program(LLVM_RC llvm-rc HINTS ${_llvm_hints} REQUIRED)
find_program(LLVM_MT llvm-mt HINTS ${_llvm_hints})

set(CMAKE_C_COMPILER "${CLANG_CL}")
set(CMAKE_CXX_COMPILER "${CLANG_CL}")
set(CMAKE_LINKER "${LLD_LINK}")
set(CMAKE_AR "${LLVM_LIB}")
set(CMAKE_RC_COMPILER "${LLVM_RC}")
if(LLVM_MT)
    set(CMAKE_MT "${LLVM_MT}")
endif()

set(_target "--target=x86_64-pc-windows-msvc")
set(_inc "/imsvc\"${XWIN_DIR}/crt/include\" /imsvc\"${XWIN_DIR}/sdk/include/ucrt\" /imsvc\"${XWIN_DIR}/sdk/include/um\" /imsvc\"${XWIN_DIR}/sdk/include/shared\" /imsvc\"${XWIN_DIR}/sdk/include/winrt\"")
set(CMAKE_C_FLAGS_INIT "${_target} ${_inc}")
set(CMAKE_CXX_FLAGS_INIT "${_target} ${_inc}")
# llvm-rc needs the SDK headers too (winres.h, windows.h).
set(CMAKE_RC_FLAGS_INIT "/I \"${XWIN_DIR}/sdk/include/um\" /I \"${XWIN_DIR}/sdk/include/shared\" /I \"${XWIN_DIR}/crt/include\" /I \"${XWIN_DIR}/sdk/include/ucrt\"")

set(_libs "/libpath:\"${XWIN_DIR}/crt/lib/x86_64\" /libpath:\"${XWIN_DIR}/sdk/lib/um/x86_64\" /libpath:\"${XWIN_DIR}/sdk/lib/ucrt/x86_64\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libs}")

# xwin ships only the release CRT, so CMake's own compiler checks build in Release too.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# Look for programs on the build machine, everything else only in the Windows SDK.
set(CMAKE_FIND_ROOT_PATH "${XWIN_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
