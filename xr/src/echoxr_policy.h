// Decisions EchoXR's runtime and launcher make, kept free of Windows and OpenXR calls so
// tests/runtime_units.cpp can check them on any machine.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace echoxr {

// --- OpenXR instance extensions ----------------------------------------------------------
//
// Windows: Revive's set. D3D11 is required, the other graphics APIs are enabled when offered.
// Wine/Proton: exactly one graphics API, the one the game renders with (GameApi). Proton's
// wineopenxr turns each D3D11, D3D12 and Vulkan request into XR_KHR_vulkan_enable, and runtimes
// such as SteamVR refuse an instance that names the same extension twice.

// The graphics API the game renders with, and so the one the field-of-view probe and the
// adapter query use.
enum class GraphicsApi { D3D11, D3D12 };

// Windows: D3D11 (the other APIs are enabled too). Wine: ECHOXR_GRAPHICS_API ("d3d11" or
// "d3d12") when set (EchoXR.exe sets it from its build table), else D3D11 when the game's exe
// imports d3d11.dll (the 2017-2019 event builds), else D3D12 (the live build, which loads its
// renderer itself).
inline GraphicsApi GameApi(bool wine, const char* hint, bool importsD3D11)
{
	if (!wine)
		return GraphicsApi::D3D11;
	if (hint && strcmp(hint, "d3d11") == 0)
		return GraphicsApi::D3D11;
	if (hint && strcmp(hint, "d3d12") == 0)
		return GraphicsApi::D3D12;
	return importsD3D11 ? GraphicsApi::D3D11 : GraphicsApi::D3D12;
}

inline std::vector<const char*> RequiredExtensions(bool wine, GraphicsApi api)
{
	if (wine && api == GraphicsApi::D3D12)
		return { "XR_KHR_win32_convert_performance_counter_time", "XR_KHR_D3D12_enable" };
	return { "XR_KHR_win32_convert_performance_counter_time", "XR_KHR_D3D11_enable" };
}

inline std::vector<const char*> OptionalExtensions(bool wine)
{
	std::vector<const char*> list;
	if (!wine)
		list = { "XR_KHR_D3D12_enable", "XR_KHR_vulkan_enable", "XR_KHR_opengl_enable" };
	for (const char* name : {
			 "XR_MND_headless",                  // the field-of-view probe without a device
			 "XR_KHR_visibility_mask",
			 "XR_KHR_composition_layer_depth",
			 "XR_KHR_composition_layer_cube",
			 "XR_KHR_composition_layer_cylinder",
			 "XR_EPIC_view_configuration_fov",
			 "XR_OCULUS_audio_device_guid",
			 "XR_FB_color_space" })
		list.push_back(name);
	return list;
}

// The extensions to enable, given what the runtime offers: every required one (instance
// creation fails if one is missing, and says which), then the optional ones it has.
inline std::vector<const char*> ChooseExtensions(bool wine, GraphicsApi api, const std::vector<std::string>& offered)
{
	std::vector<const char*> chosen = RequiredExtensions(wine, api);
	for (const char* name : OptionalExtensions(wine))
		for (const std::string& o : offered)
			if (o == name)
			{
				chosen.push_back(name);
				break;
			}
	return chosen;
}


// --- Swapchain formats ---------------------------------------------------------------------

// DXGI_FORMAT values (dxgiformat.h).
constexpr int64_t kDxgiD32FloatS8X24 = 20;
constexpr int64_t kDxgiD24UnormS8 = 45;

// AMD's Vulkan drivers have no D24S8, so through Proton the runtime may not offer it. D32S8
// keeps the stencil and is offered everywhere; a runtime that has D24S8 keeps getting it.
inline int64_t NegotiateDepthFormat(int64_t format, bool offered, bool d32s8Offered)
{
	if (format == kDxgiD24UnormS8 && !offered && d32s8Offered)
		return kDxgiD32FloatS8X24;
	return format;
}

// --- Composition layers --------------------------------------------------------------------

// ovrLayerType values (OVR_CAPI.h).
constexpr int kLayerEyeFov = 1;
constexpr int kLayerEyeFovDepth = 2;
constexpr int kLayerEyeMatrix = 5;

// Eye layers are opaque, as on Oculus's compositor: a game may leave its eye texture's alpha
// at zero, and blending by it would show nothing. Quads, cylinders and cubes keep alpha.
inline bool BlendsByAlpha(int layerType)
{
	return layerType != kLayerEyeFov && layerType != kLayerEyeFovDepth && layerType != kLayerEyeMatrix;
}

// --- Proton's VR state (HKCU\Software\Wine\VR) --------------------------------------------

// What EchoXR.exe does with the key Proton's VR set-up writes: nothing while Proton is still
// working on it (state 0), use it when it has OpenXR's Vulkan extensions, else set OpenXR up
// itself (no OpenVR runtime, the VR service wasn't up, or Proton's set-up didn't run).
enum class VrKey { Wait, Ready, SetUp };
inline VrKey DecideVrKey(bool keyExists, int64_t state, bool instanceExtensions, bool deviceExtensions)
{
	if (keyExists && state == 0)
		return VrKey::Wait;
	if (keyExists && state == 1 && instanceExtensions && deviceExtensions)
		return VrKey::Ready;
	return VrKey::SetUp;
}

// --- Adapters ----------------------------------------------------------------------------

// The adapter to create a device on: the one with the runtime's LUID, else the first one
// (a missing match used to hand a null adapter to the device creation). -1 when none.
template <typename Luid>
int PickAdapter(const std::vector<Luid>& adapters, const Luid& wanted)
{
	for (size_t i = 0; i < adapters.size(); i++)
		if (std::memcmp(&adapters[i], &wanted, sizeof(Luid)) == 0)
			return (int)i;
	return adapters.empty() ? -1 : 0;
}

} // namespace echoxr
