#pragma once
// Used by the EchoXR launcher (xr/src/launcher.cpp) and its updater (xr/src/updater.h):
// file helpers and the echovr_openxr.exe patch.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace echoxr {

inline bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
inline bool IsDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::string ReadAll(const std::wstring& path) {
    std::string s;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") || !f) return s;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

// Writes to <path>.new, then swaps it in, so a file in use fails cleanly instead of
// being left half-written. Returns 0 or the Windows error.
inline DWORD WriteAll(const std::wstring& path, const void* data, size_t size) {
    std::wstring tmp = path + L".new";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)size, &wrote, nullptr) && wrote == size;
    DWORD e = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return e ? e : ERROR_WRITE_FAULT; }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        e = GetLastError();
        DeleteFileW(tmp.c_str());
        return e;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// echovr_openxr.exe: a patched copy of the game's executable (echovr.exe itself is
// untouched). Which bytes change depends on the build, found by the exe's PE timestamp:
//   - every build: its LibOVR runtime signature check always passes, so Echo accepts
//     the unsigned EchoXR runtime.
//   - the event builds: they set themselves up as Echo VR only when their exe is named
//     echovr.exe (or echoarena.exe / echocombat.exe); under any other name they boot as
//     Lone Echo and sit on its starfield. The copy skips that name check.
// Each patch's original bytes are verified first; an unknown build is refused.
// ---------------------------------------------------------------------------
static const wchar_t* kModdedExe = L"echovr_openxr.exe";

struct ExePatch {
    DWORD rva;
    BYTE expect[16];     // the build's bytes there
    BYTE with[16];       // what replaces them (the first `len` bytes; the rest stay)
    int expectLen;
    int len;
};

struct GameBuild {
    DWORD timestamp;     // the exe's PE TimeDateStamp
    const char* name;
    const wchar_t* exe;  // the original executable, in the build's bin folder
    ExePatch patches[2];
    int patchCount;
};

static const GameBuild kBuilds[] = {
    { 0x6452DFF6, "Echo VR 34.4.631547.1 (live)", L"echovr.exe",
      { { 0x1365bd0, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x57, 0x41, 0x56 },
          { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 }, 14, 6 } },                        // mov eax,1 ; ret
      1 },
    { 0x5BC7B897, "Halloween 2018 (16.0.253636.0)", L"echovr.exe",
      { { 0xB6E2CA, { 0x83, 0xFE, 0x01, 0x74, 0x2C }, { 0x83, 0xFE, 0x01, 0xEB, 0x2C }, 5, 5 },   // signature check: je -> jmp
        { 0x957AB, { 0x75, 0x08 }, { 0x90, 0x90 }, 2, 2 } },                        // name check: jne -> nops
      2 },
};

inline DWORD PeTimestamp(const std::string& pe) {
    if (pe.size() < 0x40 || pe[0] != 'M' || pe[1] != 'Z') return 0;
    DWORD nt = *(const DWORD*)&pe[0x3C];
    if ((size_t)nt + sizeof(IMAGE_NT_HEADERS64) > pe.size() || memcmp(&pe[nt], "PE\0\0", 4)) return 0;
    return ((const IMAGE_NT_HEADERS64*)&pe[nt])->FileHeader.TimeDateStamp;
}

inline const GameBuild* FindBuild(DWORD timestamp) {
    for (const GameBuild& b : kBuilds)
        if (b.timestamp == timestamp) return &b;
    return nullptr;
}

// The known build whose executable is in dir (a bin folder, with its trailing slash), or null.
inline const GameBuild* BuildIn(const std::wstring& dir) {
    for (const GameBuild& b : kBuilds) {
        std::string data = ReadAll(dir + b.exe);
        if (!data.empty() && PeTimestamp(data) == b.timestamp) return &b;
    }
    return nullptr;
}

inline size_t RvaToOffset(const std::string& pe, DWORD rva) {
    if (pe.size() < 0x40 || pe[0] != 'M' || pe[1] != 'Z') return 0;
    DWORD nt = *(const DWORD*)&pe[0x3C];
    if ((size_t)nt + sizeof(IMAGE_NT_HEADERS64) > pe.size() || memcmp(&pe[nt], "PE\0\0", 4)) return 0;
    const IMAGE_NT_HEADERS64* h = (const IMAGE_NT_HEADERS64*)&pe[nt];
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(h);
    for (WORD i = 0; i < h->FileHeader.NumberOfSections; ++i, ++sec) {
        if ((const char*)(sec + 1) > pe.data() + pe.size()) return 0;
        DWORD size = sec->Misc.VirtualSize > sec->SizeOfRawData ? sec->Misc.VirtualSize : sec->SizeOfRawData;
        if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + size) {
            size_t off = rva - sec->VirtualAddress + sec->PointerToRawData;
            return off < pe.size() ? off : 0;
        }
    }
    return 0;
}

// Applies b's patches to data (the original executable). On failure, err says why.
inline bool PatchExe(std::string& data, const GameBuild& b, std::wstring& err) {
    for (int i = 0; i < b.patchCount; ++i) {
        const ExePatch& p = b.patches[i];
        size_t off = RvaToOffset(data, p.rva);
        if (!off || off + p.expectLen > data.size()) { err = L"the executable isn't the expected build"; return false; }
        if (memcmp(&data[off], p.expect, p.expectLen)) {
            err = memcmp(&data[off], p.with, p.len) ? L"the executable isn't the expected build (check bytes differ)"
                                                    : L"the executable itself is already patched -- restore the original first";
            return false;
        }
    }
    for (int i = 0; i < b.patchCount; ++i)
        memcpy(&data[RvaToOffset(data, b.patches[i].rva)], b.patches[i].with, b.patches[i].len);
    return true;
}

// Whether dir's echovr_openxr.exe is the current patched copy of b's executable (an older
// EchoXR may have made it with fewer patches, or the game may have been updated since).
inline bool OpenXRExeCurrent(const std::wstring& dir, const GameBuild& b) {
    std::string data = ReadAll(dir + b.exe), copy = ReadAll(dir + kModdedExe);
    std::wstring err;
    return !copy.empty() && PatchExe(data, b, err) && data == copy;
}

// dir = the build's bin folder, with its trailing slash. On failure, err says why and
// *winErr holds the Windows error (0 when the reason is the game build).
inline bool MakeOpenXRExe(const std::wstring& dir, const GameBuild& b, std::wstring& err, DWORD* winErr = nullptr) {
    if (winErr) *winErr = 0;
    std::string data = ReadAll(dir + b.exe);
    if (data.empty()) { err = L"couldn't read the game's executable"; return false; }
    if (!PatchExe(data, b, err)) return false;
    DWORD e = WriteAll(dir + kModdedExe, data.data(), data.size());
    if (e) {
        if (winErr) *winErr = e;
        err = (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED) ? L"can't write echovr_openxr.exe (in use, or the folder needs administrator)"
                                                                         : L"can't write echovr_openxr.exe (error " + std::to_wstring(e) + L")";
        return false;
    }
    return true;
}

}  // namespace echoxr
