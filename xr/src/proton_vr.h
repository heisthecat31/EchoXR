// Proton's OpenXR, prepared from inside the prefix (EchoXR.exe runs under Proton on Linux).
//
// Proton's own VR set-up runs in its steam.exe before EchoXR.exe starts. It loads the OpenVR
// runtime first and only then asks wineopenxr for OpenXR's Vulkan extensions, writing both
// into the volatile key HKCU\Software\Wine\VR. Without an OpenVR runtime (Monado or WiVRn with
// no xrizer), or when the VR service wasn't up yet, it never gets to OpenXR, and OpenXR stays
// off for that launch. EchoXR then does the OpenXR half itself, the way Proton would, and
// sets state = 1: Proton 10 and newer have wineopenxr write the values itself
// (wineopenxr_init_registry); Proton 9 asks it for them (__wineopenxr_get_extensions_internal)
// and writes them in steam.exe, which EchoXR does the same way.
#pragma once

#include <windows.h>
#include <string>

#include "echoxr_policy.h"

namespace protonvr {

typedef void (*LogFn)(const wchar_t* fmt, ...);

static const wchar_t* kKey = L"Software\\Wine\\VR";

struct KeyState
{
	bool exists = false;
	int64_t state = 0;
	bool instanceExtensions = false;
	bool deviceExtensions = false;
};

inline std::wstring ReadString(HKEY key, const wchar_t* name)
{
	wchar_t buf[4096];
	DWORD size = sizeof(buf), type = 0;
	if (RegQueryValueExW(key, name, nullptr, &type, (BYTE*)buf, &size) != ERROR_SUCCESS || type != REG_SZ)
		return L"";
	DWORD chars = size / sizeof(wchar_t);
	buf[chars < 4095 ? chars : 4095] = 0;
	return buf;
}

inline KeyState ReadKey()
{
	KeyState k;
	HKEY h;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &h) != ERROR_SUCCESS)
		return k;
	k.exists = true;
	DWORD value = 0, size = sizeof(value), type = 0;
	if (RegQueryValueExW(h, L"state", nullptr, &type, (BYTE*)&value, &size) == ERROR_SUCCESS && type == REG_DWORD)
		k.state = (int32_t)value;
	k.instanceExtensions = RegQueryValueExW(h, L"openxr_vulkan_instance_extensions", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
	k.deviceExtensions = RegQueryValueExW(h, L"openxr_vulkan_device_extensions", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
	RegCloseKey(h);
	return k;
}

// A host path (/run/user/1000/...) as the prefix sees it (Z:\run\user\1000\...).
inline std::wstring HostPath(std::wstring path)
{
	for (wchar_t& c : path)
		if (c == L'/')
			c = L'\\';
	return L"Z:" + path;
}

// The VR service, as far as the prefix can tell: the host side's word for it
// (ECHOXR_VR_SERVICE=ready, set by the launcher or echoxr-linux.sh after looking for SteamVR's
// vrserver), or Monado's and WiVRn's sockets in the host's XDG_RUNTIME_DIR. Empty when nothing
// was found; the set-up is tried anyway, with a time limit.
inline std::wstring VrService()
{
	wchar_t buf[1024];
	if (GetEnvironmentVariableW(L"ECHOXR_VR_SERVICE", buf, 1024) && !_wcsicmp(buf, L"ready"))
		return L"reported running by the launcher";
	for (const wchar_t* var : { L"WINE_HOST_XDG_RUNTIME_DIR", L"XDG_RUNTIME_DIR" })
	{
		DWORD n = GetEnvironmentVariableW(var, buf, 1024);
		if (!n || n >= 1024 || buf[0] != L'/')
			continue;
		const std::wstring dir = HostPath(buf);
		if (GetFileAttributesW((dir + L"\\monado_comp_ipc").c_str()) != INVALID_FILE_ATTRIBUTES)
			return L"Monado (" + dir + L"\\monado_comp_ipc)";
		if (GetFileAttributesW((dir + L"\\wivrn\\comp_ipc").c_str()) != INVALID_FILE_ATTRIBUTES)
			return L"WiVRn (" + dir + L"\\wivrn\\comp_ipc)";
	}
	return L"";
}

struct InitCall
{
	FARPROC initRegistry = nullptr;      // Proton 10+: BOOL CDECL wineopenxr_init_registry(void)
	FARPROC getExtensions = nullptr;     // Proton 9: int WINAPI __wineopenxr_get_extensions_internal(...)
	HKEY key = nullptr;
	bool ok = false;
};

static DWORD WINAPI CallInit(void* param)
{
	InitCall* call = (InitCall*)param;
	if (call->initRegistry)
	{
		call->ok = ((BOOL(CDECL*)())call->initRegistry)() != FALSE;
		return 0;
	}
	char* instanceExtensions = nullptr;
	char* deviceExtensions = nullptr;
	uint32_t vid = 0, pid = 0;
	typedef int(WINAPI* GetExtensions)(char**, char**, uint32_t*, uint32_t*);
	if (((GetExtensions)call->getExtensions)(&instanceExtensions, &deviceExtensions, &vid, &pid) != 0 ||
		!instanceExtensions || !deviceExtensions)
		return 0;
	// As Proton 9's steam.exe writes them (the strings belong to wineopenxr).
	call->ok =
		RegSetValueExA(call->key, "openxr_vulkan_instance_extensions", 0, REG_SZ, (const BYTE*)instanceExtensions,
			(DWORD)strlen(instanceExtensions) + 1) == ERROR_SUCCESS &&
		RegSetValueExA(call->key, "openxr_vulkan_device_extensions", 0, REG_SZ, (const BYTE*)deviceExtensions,
			(DWORD)strlen(deviceExtensions) + 1) == ERROR_SUCCESS &&
		RegSetValueExA(call->key, "openxr_vulkan_device_vid", 0, REG_DWORD, (const BYTE*)&vid, sizeof(vid)) == ERROR_SUCCESS &&
		RegSetValueExA(call->key, "openxr_vulkan_device_pid", 0, REG_DWORD, (const BYTE*)&pid, sizeof(pid)) == ERROR_SUCCESS;
	return 0;
}

// Makes sure Proton's OpenXR is on for this launch: waits for Proton's own set-up, and does
// the OpenXR part itself when that didn't. False (with the reason logged) when OpenXR can't
// be reached. didSetUp tells whether EchoXR did it (then Proton's OpenVR values are missing).
inline bool EnsureOpenXR(LogFn log, bool& didSetUp)
{
	didSetUp = false;
	KeyState k;
	echoxr::VrKey decision;
	const ULONGLONG deadline = GetTickCount64() + 10000;
	for (;;)
	{
		k = ReadKey();
		decision = echoxr::DecideVrKey(k.exists, k.state, k.instanceExtensions, k.deviceExtensions);
		if (decision != echoxr::VrKey::Wait || GetTickCount64() > deadline)
			break;
		Sleep(100);
	}

	if (decision == echoxr::VrKey::Ready)
	{
		log(L"Proton set OpenXR up itself (through its OpenVR runtime)");
		return true;
	}
	if (decision == echoxr::VrKey::Wait)
		log(L"Proton's VR set-up hasn't finished after 10 s; EchoXR sets OpenXR up itself");
	else if (!k.exists)
		log(L"Proton's VR set-up didn't run (SteamGameId unset?); EchoXR sets OpenXR up itself");
	else if (k.state == 1)
		log(L"Proton set up OpenVR but not OpenXR; EchoXR sets OpenXR up itself");
	else
		log(L"Proton found no OpenVR runtime, or no VR service (state %lld); EchoXR sets OpenXR up itself", (long long)k.state);

	const std::wstring service = VrService();
	log(L"VR service: %ls", service.empty() ? L"none seen (trying anyway)" : service.c_str());

	// Start over with a key of our own, volatile like Proton's: a saved one would make the next
	// Proton launch skip its own set-up.
	RegDeleteTreeW(HKEY_CURRENT_USER, kKey);
	HKEY key;
	DWORD disposition = 0;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr,
		&key, &disposition) != ERROR_SUCCESS)
	{
		log(L"ERROR: couldn't create HKCU\\%ls", kKey);
		return false;
	}

	HMODULE wineopenxr = LoadLibraryW(L"wineopenxr.dll");
	InitCall* call = new InitCall;
	call->key = key;
	if (wineopenxr)
	{
		call->initRegistry = GetProcAddress(wineopenxr, "wineopenxr_init_registry");
		if (!call->initRegistry)
			call->getExtensions = GetProcAddress(wineopenxr, "__wineopenxr_get_extensions_internal");
	}
	if (!call->initRegistry && !call->getExtensions)
	{
		log(L"ERROR: this Proton has no usable wineopenxr: use Proton 9 or newer, Proton Experimental or GE-Proton");
		RegCloseKey(key);
		delete call;
		return false;
	}
	log(L"Asking wineopenxr for OpenXR's Vulkan extensions (%ls)",
		call->initRegistry ? L"wineopenxr_init_registry" : L"__wineopenxr_get_extensions_internal");

	// Linux SteamVR's xrCreateInstance waits forever when SteamVR isn't running.
	HANDLE thread = CreateThread(nullptr, 0, CallInit, call, 0, nullptr);
	if (!thread || WaitForSingleObject(thread, 20000) != WAIT_OBJECT_0)
	{
		// The call may still finish later; leave its data alone (the process ends soon).
		log(L"ERROR: the OpenXR runtime didn't answer within 20 s. Is SteamVR, Monado or WiVRn running?");
		return false;
	}
	CloseHandle(thread);
	delete call;

	k = ReadKey();
	if (!k.instanceExtensions || !k.deviceExtensions)
	{
		log(L"ERROR: wineopenxr couldn't reach the OpenXR runtime. Is SteamVR, Monado or WiVRn running, "
			L"and is it the active OpenXR runtime (XR_RUNTIME_JSON or ~/.config/openxr/1/active_runtime.json)?");
		RegCloseKey(key);
		return false;
	}
	log(L"OpenXR Vulkan instance extensions: %ls", ReadString(key, L"openxr_vulkan_instance_extensions").c_str());
	log(L"OpenXR Vulkan device extensions: %ls", ReadString(key, L"openxr_vulkan_device_extensions").c_str());

	DWORD one = 1;
	RegSetValueExW(key, L"state", 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
	RegCloseKey(key);
	didSetUp = true;
	log(L"OpenXR is set up (no OpenVR runtime needed)");
	return true;
}

} // namespace protonvr
