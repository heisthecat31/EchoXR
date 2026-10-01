@echo off
setlocal
set "VARS_BAT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VARS_BAT=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not defined VARS_BAT if exist "J:\vs2026\VC\Auxiliary\Build\vcvars64.bat" set "VARS_BAT=J:\vs2026\VC\Auxiliary\Build\vcvars64.bat"
if not defined VARS_BAT (
    echo [ERROR] MSVC not found.
    exit /b 1
)
if not defined VSCMD_ARG_TGT_ARCH call "%VARS_BAT%" >nul

cd /d "%~dp0"
rem EchoXR: the OpenXR runtime (LibOVRRT64_1.dll), the OpenXR loader and the launcher
call xr\build_xr.bat
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo.
echo Built into %~dp0xr\out
