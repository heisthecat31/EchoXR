// EchoXR launcher -- starts Echo VR on an OpenXR runtime, no Oculus software. No UI: all it
// has to say goes to EchoXR\launcher.log, and how it ended to its exit code.
//
//   EchoXR.exe [--exe <name>] [--runtime steamvr|active] [--setup-only] [echo arguments...]
//
// It lives in bin\win10 next to echovr.exe and starts echovr_openxr.exe by default
// (--exe picks another). First it sets up what's missing: echovr_openxr.exe, a patched
// copy of echovr.exe (echoxr_common.h). Then, before launching:
//   1. picks the OpenXR runtime: SteamVR on Windows (--runtime active: the system's),
//      Proton's wineopenxr under Wine, which it sets up itself when Proton didn't
//      (proton_vr.h: no OpenVR runtime needed).
//   2. checks that the runtime answers and has a headset (preflight.h).
//   3. holds the "OculusHMDConnected" event. Echo's LibOVR shim calls ovr_Detect(),
//      which opens this event to decide whether a headset is present; the Oculus
//      service normally owns it. A plain named event -- no hooks, no injection.
//   4. sets LIBOVR_DLL_DIR to bin\win10\EchoXR\ -- the directory Echo's own loader
//      checks FIRST for LibOVRRT64_1.dll -- and puts that folder on PATH so the
//      runtime's openxr_loader.dll resolves. Only this launch sees these; a normal
//      launch of Echo is untouched.
// Then it starts Echo with the remaining arguments, waits for it to exit and returns its
// exit code. Its own failures have codes of their own (see Exit below and the README).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include "echoxr_common.h"
#include "preflight.h"
#include "proton_vr.h"

// EchoXR.exe's own exit codes; anything else is Echo's.
enum Exit {
    kNotInGameFolder = 2,   // not next to echovr.exe in bin\win10
    kRuntimeMissing = 3,    // EchoXR\LibOVRRT64_1.dll or openxr_loader.dll missing
    kSetupFailed = 4,       // echovr_openxr.exe couldn't be made
    kNoOpenXR = 5,          // no OpenXR runtime answered (or the VR service isn't running)
    kNoHeadset = 6,         // the runtime has no headset (not connected, or asleep)
    kStartFailed = 7,       // Echo couldn't be started
};

static FILE* g_log = nullptr;
static void Log(const wchar_t* fmt, ...) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(stdout, fmt, ap);
    va_end(ap);
    fputwc(L'\n', stdout);
    if (g_log) {
        fwprintf(g_log, L"[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
        va_start(ap, fmt);
        vfwprintf(g_log, fmt, ap);
        va_end(ap);
        fputwc(L'\n', g_log);
        fflush(g_log);
    }
}

// launcher.log keeps earlier launches (each starts with a dated line) up to about 1 MB.
static void OpenLog(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    const bool big = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
                     (data.nFileSizeHigh || data.nFileSizeLow > 1024 * 1024);
    _wfopen_s(&g_log, path.c_str(), big ? L"w" : L"a");
    if (g_log) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fwprintf(g_log, L"\n===== %04d-%02d-%02d %02d:%02d:%02d =====\n", t.wYear, t.wMonth, t.wDay,
                 t.wHour, t.wMinute, t.wSecond);
    }
}

static std::wstring ActiveOpenXRRuntime() {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime",
                     RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return buf;
    return L"";
}

// SteamVR's OpenXR manifest, from Steam's own runtime registry
// (%LOCALAPPDATA%\openvr\openvrpaths.vrpath -> "runtime": [ "<SteamVR dir>", ... ]).
static std::wstring SteamVROpenXRJson() {
    wchar_t local[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) return L"";
    FILE* f = nullptr;
    if (_wfopen_s(&f, (std::wstring(local) + L"\\openvr\\openvrpaths.vrpath").c_str(), L"rb") || !f) return L"";
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    size_t k = text.find("\"runtime\"");
    if (k == std::string::npos) return L"";
    size_t q1 = text.find('"', text.find('[', k));
    size_t q2 = text.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) return L"";
    std::string dir;
    for (size_t i = q1 + 1; i < q2; ++i) {             // unescape JSON "\\"
        if (text[i] == '\\' && i + 1 < q2) ++i;
        dir += text[i];
    }
    int wn = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
    std::wstring wdir(wn ? wn - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, &wdir[0], wn);
    std::wstring json = wdir + L"\\steamxr_win64.json";
    return GetFileAttributesW(json.c_str()) != INVALID_FILE_ATTRIBUTES ? json : L"";
}

// A setup problem: logged, and the exit code says which.
static int Fail(const std::wstring& msg, int code) {
    Log(L"ERROR: %ls (exit code %d)", msg.c_str(), code);
    if (g_log) fclose(g_log);
    return code;
}

int wmain(int argc, wchar_t** argv) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring dir = self;
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);          // the bin\win10 folder
    std::wstring xrDir = dir + L"EchoXR\\";

    std::wstring exe = echoxr::kModdedExe;      // --exe <name> picks another executable in bin\win10
    std::wstring runtimeMode = L"steamvr";      // steamvr | active
    std::wstring passArgs;
    bool setupOnly = false;                     // --setup-only: do the first-run setup, don't launch
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--exe" && i + 1 < argc) { exe = argv[++i]; continue; }
        if (a == L"--runtime" && i + 1 < argc) { runtimeMode = argv[++i]; continue; }
        if (a == L"--setup-only") { setupOnly = true; continue; }
        passArgs += L" \"" + a + L"\"";
    }
    if (echoxr::IsDir(xrDir))
        OpenLog(xrDir + L"launcher.log");

    Log(L"EchoXR launcher %hs", ECHOXR_VERSION);
    const char* (CDECL* wineVersion)() = nullptr;
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        wineVersion = (const char* (CDECL*)())GetProcAddress(ntdll, "wine_get_version");
    std::wstring gameDir = dir.substr(0, dir.size() - 1);
    if (!echoxr::Exists(dir + L"echovr.exe"))
        return Fail(L"EchoXR.exe has to sit in Echo VR's bin\\win10 folder, next to echovr.exe "
                    L"(with the EchoXR folder next to it)", kNotInGameFolder);
    if (!echoxr::Exists(xrDir + L"LibOVRRT64_1.dll") || !echoxr::Exists(xrDir + L"openxr_loader.dll"))
        return Fail(L"EchoXR\\LibOVRRT64_1.dll or EchoXR\\openxr_loader.dll is missing: copy the whole "
                    L"EchoXR folder next to EchoXR.exe", kRuntimeMissing);

    // first run: the patched game executable Echo needs to accept this runtime
    if (!_wcsicmp(exe.c_str(), echoxr::kModdedExe) && !echoxr::Exists(dir + exe)) {
        std::wstring err;
        size_t off = 0;
        if (!echoxr::MakeOpenXRExe(gameDir, err, &off))
            return Fail(L"Couldn't create echovr_openxr.exe: " + err, kSetupFailed);
        Log(L"created %ls (patched copy of echovr.exe, file offset 0x%zx)", echoxr::kModdedExe, off);
    }
    if (setupOnly) {
        Log(L"--setup-only: done, not launching");
        if (g_log) fclose(g_log);
        return 0;
    }
    std::wstring rt = ActiveOpenXRRuntime();
    Log(L"system OpenXR runtime: %ls", rt.empty() ? L"(none registered!)" : rt.c_str());
    // Under Wine/Proton (echoxr-linux.sh), the prefix's registered runtime is Proton's
    // wineopenxr, which passes every call to the Linux runtime named by the host's
    // XR_RUNTIME_JSON. Pinning a Windows manifest here would break that, so leave it.
    if (wineVersion) {
        Log(L"running under Wine %hs -- using the prefix's OpenXR runtime (wineopenxr)", wineVersion());
        runtimeMode = L"active";
    }
    if (runtimeMode == L"steamvr") {
        // Pin THIS launch to SteamVR. Other apps (e.g. Virtual Desktop's streamer) keep
        // re-registering themselves as the system runtime; the loader's XR_RUNTIME_JSON
        // override wins over that without changing any system setting.
        std::wstring svr = SteamVROpenXRJson();
        if (!svr.empty()) {
            SetEnvironmentVariableW(L"XR_RUNTIME_JSON", svr.c_str());
            Log(L"using SteamVR for this launch: %ls", svr.c_str());
        } else {
            Log(L"SteamVR not found -- falling back to the system runtime above");
        }
    }

    // Under Proton: OpenXR on for this launch, even with no OpenVR runtime.
    if (wineVersion) {
        bool didSetUp = false;
        if (!protonvr::EnsureOpenXR(Log, didSetUp))
            return Fail(L"OpenXR isn't available in this Proton prefix (see above)", kNoOpenXR);
        // Proton's OpenVR values are missing then; keep DXVK from waiting for them.
        if (didSetUp)
            SetEnvironmentVariableW(L"DXVK_NO_VR", L"1");
    }

    // The runtime answers, and has a headset.
    switch (preflight::Check(xrDir + L"openxr_loader.dll", Log)) {
    case preflight::NoRuntime: return Fail(L"no OpenXR runtime answered", kNoOpenXR);
    case preflight::NoHeadset: return Fail(L"the OpenXR runtime has no headset", kNoHeadset);
    case preflight::Ok: break;
    }

    // 1. headset-present signal for ovr_Detect()
    HANDLE hmd = CreateEventW(nullptr, TRUE, TRUE, L"OculusHMDConnected");
    DWORD evErr = GetLastError();
    if (hmd)
        Log(L"OculusHMDConnected event: %ls", evErr == ERROR_ALREADY_EXISTS ? L"already present (Oculus service running)" : L"created");
    else if (evErr == ERROR_ACCESS_DENIED)
        Log(L"OculusHMDConnected event: owned by the Oculus service -- it reports the headset itself");
    else
        Log(L"OculusHMDConnected event: could not create (error %lu) -- Echo may start without VR", evErr);

    // 2. point Echo's LibOVR loader at our runtime, and let it find openxr_loader.dll
    SetEnvironmentVariableW(L"LIBOVR_DLL_DIR", xrDir.c_str());
    wchar_t path[32767];
    DWORD n = GetEnvironmentVariableW(L"PATH", path, 32767);
    std::wstring newPath = xrDir + L";" + (n ? std::wstring(path, n) : L"");
    SetEnvironmentVariableW(L"PATH", newPath.c_str());

    std::wstring cmd = L"\"" + dir + exe + L"\"" + passArgs;
    Log(L"launching: %ls", cmd.c_str());
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring mutableCmd = cmd;
    if (!CreateProcessW(nullptr, &mutableCmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        const DWORD err = GetLastError();
        if (hmd) CloseHandle(hmd);
        return Fail(L"couldn't start " + exe + L" (error " + std::to_wstring(err) + L")", kStartFailed);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    Log(L"Echo exited with code %lu", code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (hmd) CloseHandle(hmd);
    if (g_log) fclose(g_log);
    return (int)code;
}
