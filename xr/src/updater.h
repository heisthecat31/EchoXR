#pragma once
// EchoXR self-update, used by the launcher (launcher.cpp).
//
// At most once every 20 hours (EchoXR\update_check.txt), EchoXR.exe asks GitHub for
// the latest release of heisthecat31/EchoXR. If its tag is newer than this build
// (ECHOXR_VERSION, from VERSION) and it carries an EchoXR-v<version>.zip, the
// player is asked. On yes: the zip is downloaded to %TEMP%, unpacked with Windows'
// own tar.exe, checked (EchoXR.exe + EchoXR\LibOVRRT64_1.dll at its root), and
// copied over bin\win10. The running EchoXR.exe can't be overwritten but can be
// renamed, so it moves to EchoXR.exe.old first; the new EchoXR.exe is then started
// with the same arguments and deletes the .old file.
//
// echoxr.ini: CheckForUpdates = 0 turns this off. --check-update checks now.
// ECHOXR_UPDATE=yes answers the question without asking (scripted installs, tests).
//
// Hand tracking (EchoXR Hands) is released on its own, from heisthecat31/EchoXR-Hands
// (CheckHands below). Its zip unpacks to EchoXR\Hands\, and EchoXR\Hands\version.txt
// says which release is installed.
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include "echoxr_common.h"

#ifndef ECHOXR_VERSION
#define ECHOXR_VERSION "0.0.0"
#endif

namespace updater {

static const wchar_t* kHost = L"api.github.com";
static const wchar_t* kPath = L"/repos/heisthecat31/EchoXR/releases/latest";
static const char*    kAssetPrefix = "https://github.com/heisthecat31/EchoXR/releases/download/";
static const wchar_t* kHandsPath = L"/repos/heisthecat31/EchoXR-Hands/releases/latest";
static const char*    kHandsAssetPrefix = "https://github.com/heisthecat31/EchoXR-Hands/releases/download/";

typedef void (*LogFn)(const wchar_t* fmt, ...);

// "1.2.3" / "v1.2.3" -> comparable numbers; newer() is true when a > b.
inline bool Newer(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
        std::vector<int> v;
        size_t i = (!s.empty() && (s[0] == 'v' || s[0] == 'V')) ? 1 : 0;
        while (i < s.size() && v.size() < 4) {
            int n = 0;
            bool any = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') { n = n * 10 + (s[i++] - '0'); any = true; }
            if (!any) break;
            v.push_back(n);
            if (i < s.size() && s[i] == '.') ++i; else break;
        }
        while (v.size() < 4) v.push_back(0);
        return v;
    };
    return parts(a) > parts(b);
}

// One HTTPS GET; follows redirects (release downloads go to a CDN host).
inline bool HttpsGet(const std::wstring& url, std::string& body, DWORD timeoutMs) {
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    HINTERNET s = WinHttpOpen(L"EchoXR/" TEXT(ECHOXR_VERSION), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = nullptr;
    if (c) {
        r = WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        const wchar_t* hdr = L"Accept: application/vnd.github+json\r\n";
        DWORD status = 0, len = sizeof(status);
        if (r && WinHttpSendRequest(r, hdr, (DWORD)-1L, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr) &&
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr) &&
            status == 200) {
            body.clear();
            for (;;) {
                DWORD avail = 0, got = 0;
                if (!WinHttpQueryDataAvailable(r, &avail) || !avail) break;
                size_t at = body.size();
                body.resize(at + avail);
                if (!WinHttpReadData(r, &body[at], avail, &got)) { body.resize(at); break; }
                body.resize(at + got);
            }
            ok = true;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

// The string value of the first "key": "..." at or after pos (GitHub JSON is plain).
inline std::string JsonString(const std::string& j, const std::string& key, size_t pos = 0, size_t* at = nullptr) {
    size_t k = j.find("\"" + key + "\"", pos);
    if (k == std::string::npos) return "";
    size_t q1 = j.find('"', j.find(':', k) + 1);
    size_t q2 = q1 == std::string::npos ? q1 : j.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    if (at) *at = q2;
    return j.substr(q1 + 1, q2 - q1 - 1);
}

inline std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // ASCII URLs / tags

inline bool RunWait(std::wstring cmd, DWORD& code) {
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 120000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// Copies every file under src into dst, keeping the folder structure.
inline bool CopyTree(const std::wstring& src, const std::wstring& dst, LogFn log, int& n) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring s = src + L"\\" + name, d = dst + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryW(d.c_str(), nullptr);
            ok = CopyTree(s, d, log, n) && ok;
        } else {
            SetFileAttributesW(d.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (CopyFileW(s.c_str(), d.c_str(), FALSE)) ++n;
            else { log(L"update: couldn't write %ls (error %lu)", d.c_str(), GetLastError()); ok = false; }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

inline void DeleteTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName, p = dir + L"\\" + name;
            if (name == L"." || name == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(p);
            else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(p.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// The first release asset under prefix whose name contains want and ends in .zip.
inline std::string FindZip(const std::string& json, const char* prefix, const char* want) {
    for (size_t pos = 0;;) {
        size_t at = 0;
        std::string u = JsonString(json, "browser_download_url", pos, &at);
        if (u.empty()) return "";
        pos = at;
        if (u.compare(0, strlen(prefix), prefix) == 0 && u.find(want) != std::string::npos &&
            u.size() > 4 && u.compare(u.size() - 4, 4, ".zip") == 0) return u;
    }
}

// Downloads zipUrl and unpacks it into %TEMP%\<name> with Windows' tar.exe; check
// names a file the zip has to contain. On success outUnpack is that folder (the
// caller deletes it); on failure nothing is left behind.
inline bool FetchZip(const std::string& zipUrl, const std::wstring& name, const std::wstring& check,
                     std::wstring& outUnpack, LogFn log) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring zip = std::wstring(tmp) + name + L".zip", unpack = std::wstring(tmp) + name;
    std::string data;
    if (!HttpsGet(Widen(zipUrl), data, 30000) || data.size() < 1024 || data.compare(0, 2, "PK") != 0) {
        log(L"update: download failed (%hs)", zipUrl.c_str());
        return false;
    }
    if (echoxr::WriteAll(zip, data.data(), data.size())) { log(L"update: couldn't save the download"); return false; }
    DeleteTree(unpack);
    CreateDirectoryW(unpack.c_str(), nullptr);
    DWORD code = 1;
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    bool ok = RunWait(L"\"" + std::wstring(sys) + L"\\tar.exe\" -xf \"" + zip + L"\" -C \"" + unpack + L"\"", code) &&
              code == 0 && echoxr::Exists(unpack + L"\\" + check);
    DeleteFileW(zip.c_str());
    if (!ok) {
        log(L"update: the download isn't a valid release (tar exit %lu, no %ls)", code, check.c_str());
        DeleteTree(unpack);
        return false;
    }
    outUnpack = unpack;
    return true;
}

// Returns true when an update was installed and the new EchoXR.exe was started
// (the caller then exits). dir = bin\win10 with a trailing backslash.
inline bool CheckAndUpdate(const std::wstring& dir, const std::wstring& xrDir, bool force, const std::wstring& relaunchArgs, LogFn log) {
    // left over from the previous update
    DeleteFileW((dir + L"EchoXR.exe.old").c_str());

    std::wstring stamp = xrDir + L"update_check.txt";
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULONGLONG nowSec = ((((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10000000ULL;
    if (!force) {
        std::string last = echoxr::ReadAll(stamp);
        ULONGLONG lastSec = _strtoui64(last.c_str(), nullptr, 10);
        if (lastSec && nowSec - lastSec < 20ULL * 3600) return false;
    }
    std::string stampText = std::to_string(nowSec);
    echoxr::WriteAll(stamp, stampText.data(), stampText.size());

    std::string json;
    if (!HttpsGet(std::wstring(L"https://") + kHost + kPath, json, 4000)) { log(L"update: couldn't reach GitHub -- skipped"); return false; }
    std::string tag = JsonString(json, "tag_name");
    if (tag.empty()) { log(L"update: no release information"); return false; }
    if (!Newer(tag, ECHOXR_VERSION)) { log(L"update: up to date (%hs, latest %hs)", ECHOXR_VERSION, tag.c_str()); return false; }

    // EchoXR-v* is EchoXR alone; hand tracking updates from its own releases (CheckHands),
    // and an update here leaves an installed EchoXR\Hands\ as it is.
    std::string zipUrl = FindZip(json, kAssetPrefix, "/EchoXR-v");
    if (zipUrl.empty()) { log(L"update: %hs has no EchoXR zip", tag.c_str()); return false; }
    log(L"update: %hs is available (this is %hs)", tag.c_str(), ECHOXR_VERSION);

    wchar_t env[8] = {};
    bool yes = GetEnvironmentVariableW(L"ECHOXR_UPDATE", env, 8) && !_wcsicmp(env, L"yes");
    if (!yes) {
        std::wstring q = L"EchoXR " + Widen(tag) + L" is available (you have " + Widen(ECHOXR_VERSION) +
                         L").\n\nDownload and install it now? It takes a few seconds, then Echo starts as usual.";
        yes = MessageBoxW(nullptr, q.c_str(), L"EchoXR update", MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND) == IDYES;
        if (!yes) { log(L"update: declined -- asking again tomorrow"); return false; }
    }

    std::wstring unpack;
    if (!FetchZip(zipUrl, L"EchoXR-update", L"EchoXR\\LibOVRRT64_1.dll", unpack, log) ||
        !echoxr::Exists(unpack + L"\\EchoXR.exe")) {
        if (!unpack.empty()) DeleteTree(unpack);
        MessageBoxW(nullptr, L"The update couldn't be installed. Starting the current version.", L"EchoXR update", MB_OK | MB_ICONWARNING);
        return false;
    }

    // swap: the running exe can be renamed, not overwritten
    std::wstring self = dir + L"EchoXR.exe", old = dir + L"EchoXR.exe.old";
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        log(L"update: couldn't move EchoXR.exe aside (error %lu)", GetLastError());
        DeleteTree(unpack);
        return false;
    }
    int n = 0;
    std::wstring target = dir.substr(0, dir.size() - 1);
    bool ok = CopyTree(unpack, target, log, n);
    DeleteTree(unpack);
    if (!echoxr::Exists(self)) {   // never leave the player without a launcher
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);
        log(L"update: failed -- kept the current version");
        return false;
    }
    log(L"update: installed %hs (%d files%ls)", tag.c_str(), n, ok ? L"" : L", some failed");

    std::wstring cmd = L"\"" + self + L"\"" + relaunchArgs;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        log(L"update: couldn't start the new EchoXR.exe (error %lu)", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// The installed EchoXR Hands release (EchoXR\Hands\version.txt), "" when unknown.
inline std::string HandsVersion(const std::wstring& xrDir) {
    std::string v = echoxr::ReadAll(xrDir + L"Hands\\version.txt");
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
    return v;
}

// Hand tracking from the latest heisthecat31/EchoXR-Hands release. When EchoXR\Hands\
// is missing (hand tracking switched on with AutoStartHands = 1, nothing installed),
// it's downloaded straight away. When it's installed, the release is checked at most
// once every 20 hours (EchoXR\hands_update_check.txt; force = now), and a newer one is
// offered like an EchoXR update. The caller makes sure the bridge and the settings
// window aren't running, since their files get replaced. Returns true when hand
// tracking was installed or updated.
inline bool CheckHands(const std::wstring& dir, const std::wstring& xrDir, bool force, LogFn log) {
    bool installed = echoxr::Exists(xrDir + L"Hands\\EchoXRHands.exe");
    std::wstring stamp = xrDir + L"hands_update_check.txt";
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULONGLONG nowSec = ((((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10000000ULL;
    if (installed && !force) {
        ULONGLONG lastSec = _strtoui64(echoxr::ReadAll(stamp).c_str(), nullptr, 10);
        if (lastSec && nowSec - lastSec < 20ULL * 3600) return false;
    }
    std::string stampText = std::to_string(nowSec);
    echoxr::WriteAll(stamp, stampText.data(), stampText.size());

    std::string json;
    if (!HttpsGet(std::wstring(L"https://") + kHost + kHandsPath, json, installed ? 4000 : 10000)) {
        log(L"hands update: no EchoXR Hands release found on GitHub (or GitHub unreachable) -- skipped");
        return false;
    }
    std::string tag = JsonString(json, "tag_name");
    if (tag.empty()) { log(L"hands update: no EchoXR Hands release found"); return false; }
    std::string have = installed ? HandsVersion(xrDir) : "";
    if (installed && !have.empty() && !Newer(tag, have)) {
        log(L"hands update: up to date (%hs, latest %hs)", have.c_str(), tag.c_str());
        return false;
    }
    std::string zipUrl = FindZip(json, kHandsAssetPrefix, "/EchoXR-Hands-v");
    if (zipUrl.empty()) { log(L"hands update: %hs has no EchoXR-Hands zip", tag.c_str()); return false; }

    if (installed) {
        log(L"hands update: %hs is available (installed: %hs)", tag.c_str(), have.empty() ? "unknown" : have.c_str());
        wchar_t env[8] = {};
        bool yes = GetEnvironmentVariableW(L"ECHOXR_UPDATE", env, 8) && !_wcsicmp(env, L"yes");
        if (!yes) {
            std::wstring q = L"EchoXR Hands " + Widen(tag) + L" is available (you have " +
                             (have.empty() ? std::wstring(L"an older version") : Widen(have)) +
                             L").\n\nDownload and install it now? It takes a few seconds, then Echo starts as usual.";
            yes = MessageBoxW(nullptr, q.c_str(), L"EchoXR Hands update", MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND) == IDYES;
            if (!yes) { log(L"hands update: declined -- asking again tomorrow"); return false; }
        }
    } else {
        log(L"hand tracking is on but not installed -- downloading EchoXR Hands %hs", tag.c_str());
    }

    std::wstring unpack;
    if (!FetchZip(zipUrl, L"EchoXR-Hands-update", L"EchoXR\\Hands\\EchoXRHands.exe", unpack, log)) return false;
    DeleteFileW((xrDir + L"Hands\\version.txt").c_str());   // the zip brings the new one
    int n = 0;
    bool ok = CopyTree(unpack, dir.substr(0, dir.size() - 1), log, n);
    DeleteTree(unpack);
    if (HandsVersion(xrDir).empty())   // a zip without version.txt: remember the tag
        echoxr::WriteAll(xrDir + L"Hands\\version.txt", tag.data(), tag.size());
    log(L"hands update: installed EchoXR Hands %hs (%d files%ls)", tag.c_str(), n, ok ? L"" : L", some failed");
    return true;
}

}  // namespace updater
