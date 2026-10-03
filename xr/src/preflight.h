// Before Echo starts: is an OpenXR runtime there, and does it have a headset? Asked through
// EchoXR's own openxr_loader.dll, the way the runtime DLL will ask, so a missing runtime or an
// asleep headset shows up in launcher.log (and as an exit code) instead of as Echo starting
// without VR.
#pragma once

#include <windows.h>
#include <string>

#include <openxr/openxr.h>

namespace preflight {

typedef void (*LogFn)(const wchar_t* fmt, ...);

enum Result { Ok, NoRuntime, NoHeadset };

struct Loader
{
	PFN_xrCreateInstance CreateInstance = nullptr;
	PFN_xrDestroyInstance DestroyInstance = nullptr;
	PFN_xrGetInstanceProperties GetInstanceProperties = nullptr;
	PFN_xrGetSystem GetSystem = nullptr;
	PFN_xrGetSystemProperties GetSystemProperties = nullptr;
};

struct CreateCall
{
	Loader* loader;
	XrInstance instance = XR_NULL_HANDLE;
	XrResult result = XR_ERROR_RUNTIME_FAILURE;
};

static DWORD WINAPI CallCreate(void* param)
{
	CreateCall* call = (CreateCall*)param;
	XrInstanceCreateInfo info = { XR_TYPE_INSTANCE_CREATE_INFO };
	strcpy_s(info.applicationInfo.applicationName, "EchoXR");
	strcpy_s(info.applicationInfo.engineName, "EchoXR");
	info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	call->result = call->loader->CreateInstance(&info, &call->instance);
	return 0;
}

inline const wchar_t* ResultName(XrResult r)
{
	switch (r)
	{
	case XR_ERROR_RUNTIME_UNAVAILABLE: return L"XR_ERROR_RUNTIME_UNAVAILABLE";
	case XR_ERROR_RUNTIME_FAILURE: return L"XR_ERROR_RUNTIME_FAILURE";
	case XR_ERROR_INSTANCE_LOST: return L"XR_ERROR_INSTANCE_LOST";
	case XR_ERROR_INITIALIZATION_FAILED: return L"XR_ERROR_INITIALIZATION_FAILED";
	case XR_ERROR_API_VERSION_UNSUPPORTED: return L"XR_ERROR_API_VERSION_UNSUPPORTED";
	case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return L"XR_ERROR_FORM_FACTOR_UNAVAILABLE";
	case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return L"XR_ERROR_FORM_FACTOR_UNSUPPORTED";
	case XR_ERROR_LIMIT_REACHED: return L"XR_ERROR_LIMIT_REACHED";
	default: return L"an OpenXR error";
	}
}

// loaderPath: EchoXR\openxr_loader.dll. Waits up to 20 s for the runtime to answer (Linux
// SteamVR hangs when it isn't running) and up to 15 s for a headset (one that's asleep).
inline Result Check(const std::wstring& loaderPath, LogFn log)
{
	HMODULE dll = LoadLibraryExW(loaderPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!dll)
	{
		log(L"ERROR: couldn't load %ls (error %lu)", loaderPath.c_str(), GetLastError());
		return NoRuntime;
	}
	Loader loader;
	loader.CreateInstance = (PFN_xrCreateInstance)GetProcAddress(dll, "xrCreateInstance");
	loader.DestroyInstance = (PFN_xrDestroyInstance)GetProcAddress(dll, "xrDestroyInstance");
	loader.GetInstanceProperties = (PFN_xrGetInstanceProperties)GetProcAddress(dll, "xrGetInstanceProperties");
	loader.GetSystem = (PFN_xrGetSystem)GetProcAddress(dll, "xrGetSystem");
	loader.GetSystemProperties = (PFN_xrGetSystemProperties)GetProcAddress(dll, "xrGetSystemProperties");
	if (!loader.CreateInstance || !loader.DestroyInstance || !loader.GetInstanceProperties || !loader.GetSystem ||
		!loader.GetSystemProperties)
	{
		log(L"ERROR: %ls isn't an OpenXR loader", loaderPath.c_str());
		return NoRuntime;
	}

	CreateCall* call = new CreateCall{ &loader };
	HANDLE thread = CreateThread(nullptr, 0, CallCreate, call, 0, nullptr);
	if (!thread || WaitForSingleObject(thread, 20000) != WAIT_OBJECT_0)
	{
		// The call may still finish later; leave its data alone (the process ends soon).
		log(L"ERROR: the OpenXR runtime didn't answer within 20 s. Is SteamVR, Monado or WiVRn running?");
		return NoRuntime;
	}
	CloseHandle(thread);
	const XrResult created = call->result;
	const XrInstance instance = call->instance;
	delete call;
	if (XR_FAILED(created))
	{
		log(L"ERROR: no OpenXR runtime answered (%ls, %d). Start SteamVR, Monado or WiVRn, "
			L"and make it the active OpenXR runtime.", ResultName(created), (int)created);
		return NoRuntime;
	}

	XrInstanceProperties props = { XR_TYPE_INSTANCE_PROPERTIES };
	if (XR_SUCCEEDED(loader.GetInstanceProperties(instance, &props)))
		log(L"OpenXR runtime: %hs %u.%u.%u", props.runtimeName, XR_VERSION_MAJOR(props.runtimeVersion),
			XR_VERSION_MINOR(props.runtimeVersion), XR_VERSION_PATCH(props.runtimeVersion));

	Result result = NoHeadset;
	XrSystemGetInfo getInfo = { XR_TYPE_SYSTEM_GET_INFO };
	getInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	const ULONGLONG deadline = GetTickCount64() + 15000;
	bool waited = false;
	for (;;)
	{
		XrSystemId system = XR_NULL_SYSTEM_ID;
		XrResult rs = loader.GetSystem(instance, &getInfo, &system);
		if (XR_SUCCEEDED(rs))
		{
			XrSystemProperties sys = { XR_TYPE_SYSTEM_PROPERTIES };
			loader.GetSystemProperties(instance, system, &sys);
			log(L"Headset: %hs", sys.systemName[0] ? sys.systemName : "(unnamed)");
			result = Ok;
			break;
		}
		if (rs != XR_ERROR_FORM_FACTOR_UNAVAILABLE || GetTickCount64() > deadline)
		{
			log(L"ERROR: the OpenXR runtime has no headset (%ls, %d). Is it connected and awake?",
				ResultName(rs), (int)rs);
			break;
		}
		if (!waited)
			log(L"No headset yet (asleep?); waiting up to 15 s");
		waited = true;
		Sleep(500);
	}
	loader.DestroyInstance(instance);
	return result;
}

} // namespace preflight
