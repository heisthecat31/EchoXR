#include "Runtime.h"
#include "Common.h"
#include "version.h"
#include "../../src/echoxr_policy.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <algorithm>
#include <vector>
#include <memory>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// The extensions EchoXR enables come from echoxr_policy.h (ChooseExtensions): Revive's set on
// Windows, and a single graphics API (D3D12) under Wine.

Runtime::HackInfo Runtime::s_known_hacks[] = {
	{ nullptr, "SteamVR/OpenXR", HACK_VALVE_INDEX_PROFILE, 0, 0, true },
	{ nullptr, "SteamVR/OpenXR", HACK_BROKEN_LINE_LOOP, 0, 0x100000000, true },
	{ nullptr, "SteamVR/OpenXR", HACK_MIN_HAPTIC_DURATION, 0, 0, true },
	{ "echovr.exe", nullptr, HACK_FORCE_FOV_FALLBACK, 0, 0, true },
	{ "echovr_openxr.exe", nullptr, HACK_FORCE_FOV_FALLBACK, 0, 0, true },   // EchoXR's launch copy
	{ "loneecho.exe", nullptr, HACK_FORCE_FOV_FALLBACK, 0, 0, true },
};

Runtime& Runtime::Get()
{
	static Runtime instance;
	return instance;
}

// EchoXR: whether the game's exe imports d3d11.dll (the event builds render with D3D11; the
// live build loads its D3D12 renderer itself and imports neither).
static bool ExeImportsD3D11()
{
	const BYTE* base = (const BYTE*)GetModuleHandleW(nullptr);
	if (!base)
		return false;
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
	const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return false;
	for (const IMAGE_IMPORT_DESCRIPTOR* d = (const IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; ++d)
		if (_stricmp((const char*)(base + d->Name), "d3d11.dll") == 0)
			return true;
	return false;
}

ovrResult Runtime::CreateInstance(XrInstance* out_Instance, const ovrInitParams* params)
{
	MinorVersion = params && params->Flags & ovrInit_RequestVersion ?
		params->RequestedMinorVersion : OVR_MINOR_VERSION;

	uint32_t size;
	std::vector<XrExtensionProperties> properties;
	CHK_XR(xrEnumerateInstanceExtensionProperties(nullptr, 0, &size, nullptr));
	properties.resize(size);
	for (XrExtensionProperties& props : properties)
		props = XR_TYPE(EXTENSION_PROPERTIES);
	CHK_XR(xrEnumerateInstanceExtensionProperties(nullptr, (uint32_t)properties.size(), &size, properties.data()));

	const char* (CDECL* wineVersion)() = nullptr;
	if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
		wineVersion = (const char* (CDECL*)())GetProcAddress(ntdll, "wine_get_version");
	Wine = wineVersion != nullptr;

	char hint[16] = {};
	GetEnvironmentVariableA("ECHOXR_GRAPHICS_API", hint, sizeof(hint));
	const echoxr::GraphicsApi api = echoxr::GameApi(Wine, hint[0] ? hint : nullptr, ExeImportsD3D11());
	Api = (int)api;
	if (Wine)
		EchoXR_Log("Under Wine one graphics API: %s", api == echoxr::GraphicsApi::D3D11 ? "D3D11" : "D3D12");

	std::vector<std::string> offered;
	for (const XrExtensionProperties& props : properties)
		offered.push_back(props.extensionName);
	m_extensions = echoxr::ChooseExtensions(Wine, api, offered);
	for (const char* required : echoxr::RequiredExtensions(Wine, api))
		if (std::find(offered.begin(), offered.end(), required) == offered.end())
			EchoXR_Log("The OpenXR runtime doesn't offer %s, which EchoXR needs", required);

	Headless = Supports(XR_MND_HEADLESS_EXTENSION_NAME);
	VisibilityMask = Supports(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
	CompositionDepth = Supports(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
	CompositionCube = Supports(XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME);
	CompositionCylinder = Supports(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
	AudioDevice = Supports(XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME);
	ColorSpace = Supports(XR_FB_COLOR_SPACE_EXTENSION_NAME);

	XrInstanceCreateInfo createInfo = XR_TYPE(INSTANCE_CREATE_INFO);
	// EchoXR: request OpenXR 1.0. Built against the 1.1 SDK, XR_CURRENT_API_VERSION asks for 1.1,
	// which 1.0 runtimes reject with XR_ERROR_API_VERSION_UNSUPPORTED (-> ovrError_ServiceVersion, -3004).
	createInfo.applicationInfo = { "Revive", REV_VERSION_INT, "Revive", REV_VERSION_INT, XR_API_VERSION_1_0 };
	createInfo.enabledExtensionCount = (uint32_t)m_extensions.size();
	createInfo.enabledExtensionNames = m_extensions.data();
	CHK_XR(xrCreateInstance(&createInfo, out_Instance));
	{
		if (Wine)
			EchoXR_Log("Running under Wine %s: one graphics API, D3D12", wineVersion());
		else
			EchoXR_Log("Running on Windows");
		XrInstanceProperties ip = XR_TYPE(INSTANCE_PROPERTIES);
		if (XR_SUCCEEDED(xrGetInstanceProperties(*out_Instance, &ip)))
			EchoXR_Log("OpenXR runtime: %s %u.%u.%u", ip.runtimeName, (unsigned)XR_VERSION_MAJOR(ip.runtimeVersion),
			           (unsigned)XR_VERSION_MINOR(ip.runtimeVersion), (unsigned)XR_VERSION_PATCH(ip.runtimeVersion));
		for (const char* e : m_extensions) EchoXR_Log("  enabled extension: %s", e);
		EchoXR_Log("Oculus SDK minor version requested by the game: %d", MinorVersion);
	}

	char filepath[MAX_PATH];
	GetModuleFileNameA(NULL, filepath, MAX_PATH);
	char* filename = PathFindFileNameA(filepath);

	XrInstanceProperties props = XR_TYPE(INSTANCE_PROPERTIES);
	CHK_XR(xrGetInstanceProperties(*out_Instance, &props));

	for (auto& hack : s_known_hacks)
	{
		if ((!hack.m_filename || _stricmp(filename, hack.m_filename) == 0) &&
			(!hack.m_runtime || strcmp(props.runtimeName, hack.m_runtime) == 0) &&
			(!hack.m_versionend || hack.m_versionstart <= props.runtimeVersion &&
				props.runtimeVersion < hack.m_versionend))
		{
			if (hack.m_usehack)
				m_hacks.emplace(hack.m_hack, hack);
		}
		else if (!hack.m_usehack)
		{
			m_hacks.emplace(hack.m_hack, hack);
		}
	}
	return ovrSuccess;
}

bool Runtime::UseHack(Hack hack)
{
	return m_hacks.find(hack) != m_hacks.end();
}

bool Runtime::Supports(const char* extensionName)
{
	auto findExtension = [extensionName](const char* extension)
	{
		return strcmp(extension, extensionName) == 0;
	};

	return std::any_of(m_extensions.begin(), m_extensions.end(), findExtension);
}
