@echo off
rem Builds EchoXR with MSVC and packages the release zip (build\windows-msvc\release).
rem Needs Visual Studio (C++ workload), CMake and Ninja; Python for the packaging.
setlocal
if defined VSCMD_ARG_TGT_ARCH goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio not found. Run this from an "x64 Native Tools" prompt.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [ERROR] MSVC x64 tools not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
:build
cd /d "%~dp0"
cmake --preset windows-msvc || exit /b 1
cmake --build --preset windows-msvc || exit /b 1
ctest --preset windows-msvc || exit /b 1
python tools\make_release.py || exit /b 1
