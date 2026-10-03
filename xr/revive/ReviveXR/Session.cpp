#include "OVR_CAPI.h"
#include "XR_Math.h"
#include "Common.h"
#include "Session.h"
#include "Runtime.h"
#include "InputManager.h"

#include "../../src/echoxr_policy.h"

#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

ovrResult ovrHmdStruct::InitSession(XrInstance instance)
{
	memset(FrameStats, 0, sizeof(FrameStats));
	for (int i = 0; i < ovrMaxProvidedFrameStats; i++)
		FrameStats[i].type = XR_TYPE_FRAME_STATE;
	CurrentFrame = FrameStats;
	Instance = instance;
	TrackingOrigin = ovrTrackingOrigin_EyeLevel;
	SystemProperties = XR_TYPE(SYSTEM_PROPERTIES);
	SystemColorSpace = XR_TYPE(SYSTEM_COLOR_SPACE_PROPERTIES_FB);

	// Initialize view structures
	// EchoXR: only chain the EPIC fov struct when that extension is enabled. SteamVR
	// validates next-chains strictly and fails xrEnumerateViewConfigurationViews with
	// XR_ERROR_VALIDATION_FAILURE for a struct from a disabled extension; the fallback
	// below already derives the FOV without it.
	const bool epicFov = Runtime::Get().Supports(XR_EPIC_VIEW_CONFIGURATION_FOV_EXTENSION_NAME);
	for (int i = 0; i < ovrEye_Count; i++)
	{
		ViewConfigs[i] = XR_TYPE(VIEW_CONFIGURATION_VIEW);
		ViewFov[i] = XR_TYPE(VIEW_CONFIGURATION_VIEW_FOV_EPIC);
		ViewPoses[i] = XR_TYPE(VIEW);
		ViewConfigs[i].next = epicFov ? &ViewFov[i] : nullptr;
	}

	XrSystemGetInfo systemInfo = XR_TYPE(SYSTEM_GET_INFO);
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	CHK_XR(xrGetSystem(Instance, &systemInfo, &System));
	if (Runtime::Get().ColorSpace)
		SystemProperties.next = &SystemColorSpace;
	CHK_XR(xrGetSystemProperties(Instance, System, &SystemProperties));

	uint32_t numViews;
	CHK_XR(xrEnumerateViewConfigurationViews(Instance, System, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, ovrEye_Count, &numViews, ViewConfigs));
	assert(numViews == ovrEye_Count);

	// The adapter the runtime renders on. EchoXR: from the D3D12 requirements under Wine,
	// where D3D11 isn't enabled.
	CHK_OVR(QueryAdapter());

	// The headset's field of view: from the EPIC extension when the game doesn't need more,
	// else from a short-lived session (ProbeViews).
	if (Runtime::Get().MinorVersion >= 17 && Runtime::Get().Supports(XR_EPIC_VIEW_CONFIGURATION_FOV_EXTENSION_NAME) &&
		!Runtime::Get().UseHack(Runtime::HACK_FORCE_FOV_FALLBACK))
	{
		for (int i = 0; i < ovrEye_Count; i++)
		{
			ViewPoses[i].fov = ViewFov[i].recommendedFov;
			ViewPoses[i].pose = XR::Posef::Identity();
		}
	}
	else
	{
		CHK_OVR(ProbeViews());
		for (int i = 0; i < ovrEye_Count; i++)
		{
			ViewFov[i].recommendedFov = ViewPoses[i].fov;
			ViewFov[i].maxMutableFov = ViewPoses[i].fov;
		}
	}

	// Calculate the pixels per tan angle
	for (int i = 0; i < ovrEye_Count; i++)
	{
		const XR::FovPort fov(ViewFov[i].recommendedFov);
		PixelsPerTan[i] = OVR::Vector2f(
			(float)ViewConfigs[i].recommendedImageRectWidth / (fov.LeftTan + fov.RightTan),
			(float)ViewConfigs[i].recommendedImageRectHeight / (fov.UpTan + fov.DownTan)
		);
	}

	// Initialize input manager
	Input.reset(new InputManager(Instance));
	return ovrSuccess;
}

ovrResult ovrHmdStruct::QueryAdapter()
{
	static_assert(sizeof(LUID) == sizeof(ovrGraphicsLuid), "The adapter LUID needs to fit in ovrGraphicsLuid");
	if (echoxr::ProbeApi(Runtime::Get().Wine) == echoxr::GraphicsApi::D3D12)
	{
		XR_FUNCTION(Instance, GetD3D12GraphicsRequirementsKHR);
		XrGraphicsRequirementsD3D12KHR graphicsReq = XR_TYPE(GRAPHICS_REQUIREMENTS_D3D12_KHR);
		CHK_XR(GetD3D12GraphicsRequirementsKHR(Instance, System, &graphicsReq));
		memcpy(&Adapter, &graphicsReq.adapterLuid, sizeof(ovrGraphicsLuid));
	}
	else
	{
		XR_FUNCTION(Instance, GetD3D11GraphicsRequirementsKHR);
		XrGraphicsRequirementsD3D11KHR graphicsReq = XR_TYPE(GRAPHICS_REQUIREMENTS_D3D11_KHR);
		CHK_XR(GetD3D11GraphicsRequirementsKHR(Instance, System, &graphicsReq));
		memcpy(&Adapter, &graphicsReq.adapterLuid, sizeof(ovrGraphicsLuid));
	}
	return ovrSuccess;
}

// Reads the headset's field of view (and the play area's bounds) from a short-lived session,
// before the game has handed over its graphics device: Echo asks for its render sizes first.
// EchoXR: with no device where the runtime offers XR_MND_headless; else on a temporary
// device of the API the game uses on this platform (D3D12 under Wine, so Proton's bridge
// never sees two graphics APIs), on the runtime's adapter or else the first one. Every
// runtime gets the wait for READY: SteamVR and WMR refuse to locate views before it.
ovrResult ovrHmdStruct::ProbeViews()
{
	using Microsoft::WRL::ComPtr;
	const bool d3d12 = echoxr::ProbeApi(Runtime::Get().Wine) == echoxr::GraphicsApi::D3D12;
	ComPtr<ID3D11Device> device11;
	ComPtr<ID3D12Device> device12;
	ComPtr<ID3D12CommandQueue> queue12;
	XrGraphicsBindingD3D11KHR binding11 = XR_TYPE(GRAPHICS_BINDING_D3D11_KHR);
	XrGraphicsBindingD3D12KHR binding12 = XR_TYPE(GRAPHICS_BINDING_D3D12_KHR);
	const void* binding = nullptr;
	const char* how = "no graphics device (XR_MND_headless)";

	if (!Runtime::Get().Headless)
	{
		ComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
			return ovrError_IncompatibleGPU;
		std::vector<ComPtr<IDXGIAdapter1>> adapters;
		std::vector<LUID> luids;
		ComPtr<IDXGIAdapter1> candidate;
		for (UINT i = 0; factory->EnumAdapters1(i, &candidate) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			if (FAILED(candidate->GetDesc1(&desc)))
				continue;
			adapters.push_back(candidate);
			luids.push_back(desc.AdapterLuid);
		}
		LUID wanted;
		memcpy(&wanted, &Adapter, sizeof(wanted));
		const int pick = echoxr::PickAdapter(luids, wanted);
		if (pick < 0)
		{
			EchoXR_Log("Field-of-view probe: no graphics adapter found");
			return ovrError_IncompatibleGPU;
		}
		if (memcmp(&luids[pick], &wanted, sizeof(wanted)) != 0)
			EchoXR_Log("Field-of-view probe: no adapter has the runtime's LUID, using the first one");

		if (d3d12)
		{
			D3D12_COMMAND_QUEUE_DESC queueDesc = {};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			if (FAILED(D3D12CreateDevice(adapters[pick].Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12))) ||
				FAILED(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue12))))
			{
				EchoXR_Log("Field-of-view probe: couldn't create a D3D12 device");
				return ovrError_IncompatibleGPU;
			}
			binding12.device = device12.Get();
			binding12.queue = queue12.Get();
			binding = &binding12;
			how = "a temporary D3D12 device";
		}
		else
		{
			if (FAILED(D3D11CreateDevice(adapters[pick].Get(), D3D_DRIVER_TYPE_UNKNOWN, 0, 0,
				NULL, 0, D3D11_SDK_VERSION, &device11, nullptr, nullptr)))
			{
				EchoXR_Log("Field-of-view probe: couldn't create a D3D11 device");
				return ovrError_IncompatibleGPU;
			}
			binding11.device = device11.Get();
			binding = &binding11;
			how = "a temporary D3D11 device";
		}
	}
	EchoXR_Log("Field-of-view probe: a session with %s", how);

	XrSession probe = XR_NULL_HANDLE;
	XrSessionCreateInfo createInfo = XR_TYPE(SESSION_CREATE_INFO);
	createInfo.next = binding;
	createInfo.systemId = System;
	CHK_XR(xrCreateSession(Instance, &createInfo, &probe));

	XrSpace view = XR_NULL_HANDLE;
	XrReferenceSpaceCreateInfo spaceInfo = XR_TYPE(REFERENCE_SPACE_CREATE_INFO);
	spaceInfo.poseInReferenceSpace = XR::Posef::Identity();
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	ovrResult result = ResultToOvrResult(xrCreateReferenceSpace(probe, &spaceInfo, &view));

	// Wait (bounded) until the runtime makes the session ready, then begin it.
	if (OVR_SUCCESS(result))
	{
		result = ovrError_Timeout;
		XrEventDataBuffer event;
		const XrEventDataSessionStateChanged& stateChanged =
			reinterpret_cast<XrEventDataSessionStateChanged&>(event);
		const auto deadline = std::chrono::steady_clock::now() + 10s;
		while (std::chrono::steady_clock::now() < deadline)
		{
			event = XR_TYPE(EVENT_DATA_BUFFER);
			XrResult polled = xrPollEvent(Instance, &event);
			if (polled == XR_EVENT_UNAVAILABLE)
			{
				std::this_thread::sleep_for(10ms);
				continue;
			}
			if (XR_FAILED(polled))
			{
				EchoXR_LogFail("xrPollEvent", (int)polled, __FILE__, __LINE__);
				result = ResultToOvrResult(polled);
				break;
			}
			if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED && stateChanged.session == probe &&
				stateChanged.state == XR_SESSION_STATE_READY)
			{
				result = ovrSuccess;
				break;
			}
		}
		if (result == ovrError_Timeout)
			EchoXR_Log("Field-of-view probe: the session didn't become ready within 10 s (headset asleep, or the VR runtime not ready)");
	}

	if (OVR_SUCCESS(result))
	{
		XrSessionBeginInfo beginInfo = XR_TYPE(SESSION_BEGIN_INFO);
		beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		XrResult rs = xrBeginSession(probe, &beginInfo);
		if (XR_FAILED(rs))
		{
			EchoXR_LogFail("xrBeginSession (field-of-view probe)", (int)rs, __FILE__, __LINE__);
			result = ResultToOvrResult(rs);
		}
	}

	if (OVR_SUCCESS(result))
	{
		uint32_t numViews = 0;
		XrViewLocateInfo locateInfo = XR_TYPE(VIEW_LOCATE_INFO);
		XrViewState viewState = XR_TYPE(VIEW_STATE);
		locateInfo.space = view;
		locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locateInfo.displayTime = AbsTimeToXrTime(Instance, ovr_GetTimeInSeconds());
		XrResult rs = xrLocateViews(probe, &locateInfo, &viewState, ovrEye_Count, &numViews, ViewPoses);
		if (XR_FAILED(rs))
		{
			EchoXR_LogFail("xrLocateViews (field-of-view probe)", (int)rs, __FILE__, __LINE__);
			result = ResultToOvrResult(rs);
		}
		else
		{
			// Missing bounds (no play area set up) leave zeros, as before.
			xrGetReferenceSpaceBoundsRect(probe, XR_REFERENCE_SPACE_TYPE_STAGE, &bounds);
		}
	}

	if (view != XR_NULL_HANDLE)
		xrDestroySpace(view);
	xrDestroySession(probe);
	return result;
}

ovrResult ovrHmdStruct::StartSession(void* graphicsBinding)
{
	if (Session)
		return ovrError_InvalidOperation;

	XrSessionCreateInfo createInfo = XR_TYPE(SESSION_CREATE_INFO);
	createInfo.next = graphicsBinding;
	createInfo.systemId = System;
	CHK_XR(xrCreateSession(Instance, &createInfo, &Session));
	memset(&SessionStatus, 0, sizeof(SessionStatus));

	// Attach it to the InputManager
	if (Input)
		Input->AttachSession(Session);

	// Create reference spaces
	XrReferenceSpaceCreateInfo spaceInfo = XR_TYPE(REFERENCE_SPACE_CREATE_INFO);
	spaceInfo.poseInReferenceSpace = XR::Posef::Identity();
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &ViewSpace));
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &OriginSpaces[ovrTrackingOrigin_EyeLevel]));
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &TrackingSpaces[ovrTrackingOrigin_EyeLevel]));
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &OriginSpaces[ovrTrackingOrigin_FloorLevel]));
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &TrackingSpaces[ovrTrackingOrigin_FloorLevel]));

	// Update the visibility mask for both eyes
	if (Runtime::Get().VisibilityMask)
	{
		for (uint32_t i = 0; i < ovrEye_Count; i++)
		{
			UpdateStencil((ovrEyeType)i, XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR);
			UpdateStencil((ovrEyeType)i, XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR);
			UpdateStencil((ovrEyeType)i, XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR);
		}
	}

	// Enumerate formats
	uint32_t formatCount = 0;
	CHK_XR(xrEnumerateSwapchainFormats(Session, 0, &formatCount, nullptr));
	SupportedFormats.resize(formatCount);
	CHK_XR(xrEnumerateSwapchainFormats(Session, (uint32_t)SupportedFormats.size(), &formatCount, SupportedFormats.data()));
	assert(formatCount == SupportedFormats.size());

	Running.second.notify_all();

	return ovrSuccess;
}

ovrResult ovrHmdStruct::BeginSession()
{
	XrSessionBeginInfo beginInfo = XR_TYPE(SESSION_BEGIN_INFO);
	beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	CHK_XR(xrBeginSession(Session, &beginInfo));

	// EchoXR: no frame is begun here any more. This runs on whichever thread polls the
	// session status, and waiting for a frame here raced the game's own frame calls on its
	// render thread. The eye-level origin is recentered after the first xrWaitFrame instead
	// (ovr_WaitToBeginFrame), and a game that only calls ovr_SubmitFrame gets its first frame
	// opened there.
	FrameBegun = false;
	RecenterPending = true;
	return ovrSuccess;
}

ovrResult ovrHmdStruct::EndSession()
{
	FrameBegun = false;
	CHK_XR(xrEndSession(Session));
	return ovrSuccess;
}

ovrResult ovrHmdStruct::DestroySession()
{
	if (!Session)
		return ovrError_InvalidOperation;

	if (Input)
		Input->AttachSession(XR_NULL_HANDLE);

	FrameBegun = false;
	CHK_XR(xrDestroySession(Session));
	Session = XR_NULL_HANDLE;
	ViewSpace = XR_NULL_HANDLE;
	for (uint32_t i = 0; i < ovrTrackingOrigin_Count; i++)
	{
		OriginSpaces[i] = XR_NULL_HANDLE;
		TrackingSpaces[i] = XR_NULL_HANDLE;
	}
	return ovrSuccess;
}

ovrResult ovrHmdStruct::LocateViews(XrView out_Views[ovrEye_Count], XrViewStateFlags* out_Flags) const
{
	if (!Session)
		return ovrError_InvalidSession;

	uint32_t numViews;
	XrViewLocateInfo locateInfo = XR_TYPE(VIEW_LOCATE_INFO);
	XrViewState viewState = XR_TYPE(VIEW_STATE);
	locateInfo.space = ViewSpace;
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = AbsTimeToXrTime(Instance, ovr_GetTimeInSeconds());
	CHK_XR(xrLocateViews(Session, &locateInfo, &viewState, ovrEye_Count, &numViews, out_Views));
	assert(numViews == ovrEye_Count);
	if (out_Flags)
		*out_Flags = viewState.viewStateFlags;
	return ovrSuccess;
}

ovrResult ovrHmdStruct::UpdateStencil(ovrEyeType view, XrVisibilityMaskTypeKHR type)
{
	if (!Session)
		return ovrError_InvalidSession;

	XR_FUNCTION(Instance, GetVisibilityMaskKHR);

	VisibilityMask& result = VisibilityMasks[view][type];
	XrVisibilityMaskKHR mask = XR_TYPE(VISIBILITY_MASK_KHR);
	CHK_XR(GetVisibilityMaskKHR(Session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view, type, &mask));
	if (!mask.vertexCountOutput || !mask.indexCountOutput)
		return ovrError_Unsupported;

	result.first.resize(mask.vertexCountOutput);
	result.second.resize(mask.indexCountOutput);

	mask.vertexCapacityInput = (uint32_t)result.first.size();
	mask.vertices = result.first.data();
	mask.indexCapacityInput = (uint32_t)result.second.size();
	mask.indices = result.second.data();
	CHK_XR(GetVisibilityMaskKHR(Session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view, type, &mask));

	if (type == XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR && Runtime::Get().UseHack(Runtime::HACK_BROKEN_LINE_LOOP))
	{
		// There are actually only 27 valid vertices in this line loop
		result.first.resize(27);
		result.second.resize(27);
	}
	return ovrSuccess;
}

ovrResult ovrHmdStruct::RecenterSpace(ovrTrackingOrigin origin, XrSpace anchor, ovrPosef offset)
{
	std::lock_guard<std::shared_mutex> lk(TrackingMutex);
	XrSpaceLocation location = XR_TYPE(SPACE_LOCATION);
	CHK_XR(xrLocateSpace(anchor, OriginSpaces[origin], (*CurrentFrame).predictedDisplayTime, &location));

	if (!(location.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT)))
		return ovrError_InvalidHeadsetOrientation;

	// Get the yaw orientation from the view pose
	float yaw;
	XR::Quatf(location.pose.orientation).GetYawPitchRoll(&yaw, nullptr, nullptr);

	// Construct the new origin pose
	XR::Posef newOrigin(OVR::Quatf(OVR::Axis_Y, yaw), XR::Vector3f(location.pose.position));

	// For floor level spaces we keep the height at the floor
	if (origin == ovrTrackingOrigin_FloorLevel)
		newOrigin.Translation.y = 0.0f;

	// Replace the tracking space with the newly calibrated one
	XrReferenceSpaceCreateInfo spaceInfo = XR_TYPE(REFERENCE_SPACE_CREATE_INFO);
	spaceInfo.referenceSpaceType = static_cast<XrReferenceSpaceType>(XR_REFERENCE_SPACE_TYPE_LOCAL + origin);
	spaceInfo.poseInReferenceSpace = XR::Posef(newOrigin * offset);
	CHK_XR(xrDestroySpace(TrackingSpaces[origin]));
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &TrackingSpaces[origin]));
	return ovrSuccess;
}

bool ovrHmdStruct::SupportsFormat(int64_t format) const
{
	return std::find(SupportedFormats.begin(), SupportedFormats.end(), format) != SupportedFormats.end();
}
