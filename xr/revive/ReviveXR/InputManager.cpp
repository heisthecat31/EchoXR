#include "InputManager.h"
#include "Common.h"
#include "Session.h"
#include "Runtime.h"
#include "OVR_CAPI.h"
#include "XR_Math.h"
#include "../../src/echoxr_pose.h"
#include "../../src/echoxr_settings.h"

#include <Windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <vector>

// EchoXR.txt, read again when it changes (xr/src/settings_file.cpp).
echoxr::ControllerSettings EchoXR_Controls();

namespace
{
	echoxr::Pose FromXr(const XrPosef& p)
	{
		return { { p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w },
			{ p.position.x, p.position.y, p.position.z } };
	}

	echoxr::Vec3 FromXr(const XrVector3f& v) { return { v.x, v.y, v.z }; }
}

// Oculus SDK defines that constant vibration lasts 2.5 seconds
#define MAX_VIBRATION_DURATION 2500000000i64

XrPath InputManager::s_SubActionPaths[ovrHand_Count] = { XR_NULL_PATH, XR_NULL_PATH };

InputManager::InputManager(XrInstance instance)
	: m_InputDevices()
{
	s_SubActionPaths[ovrHand_Left] = GetXrPath("/user/hand/left");
	s_SubActionPaths[ovrHand_Right] = GetXrPath("/user/hand/right");

	m_InputDevices.push_back(new OculusTouch(instance));
	m_InputDevices.push_back(new XboxGamepad(instance));
	m_InputDevices.push_back(new OculusRemote(instance));

	for (const InputDevice* device : m_InputDevices)
	{
		device->GetActiveSets(m_ActionSets);

		// Suggest bindings for every profile a device supports, the runtime picks
		// the one that best matches the connected controllers. A runtime that
		// can't remap a single profile to new hardware would otherwise leave the
		// controllers without any binding, so they never report a pose.
		std::vector<ProfileBindings> profiles;
		device->GetSuggestedBindings(profiles);
		for (const ProfileBindings& profile : profiles)
		{
			XrInteractionProfileSuggestedBinding suggestedBinding = XR_TYPE(INTERACTION_PROFILE_SUGGESTED_BINDING);
			suggestedBinding.countSuggestedBindings = (uint32_t)profile.Bindings.size();
			suggestedBinding.suggestedBindings = profile.Bindings.data();
			suggestedBinding.interactionProfile = profile.Profile;
			XrResult rs = xrSuggestInteractionProfileBindings(instance, &suggestedBinding);
			if (XR_FAILED(rs))
				EchoXR_LogFail("xrSuggestInteractionProfileBindings", (int)rs, __FILE__, __LINE__);
		}
	}
}

void InputManager::LogInteractionProfiles(XrInstance instance, XrSession session)
{
	for (int i = 0; i < ovrHand_Count; i++)
	{
		XrInteractionProfileState state = XR_TYPE(INTERACTION_PROFILE_STATE);
		if (XR_FAILED(xrGetCurrentInteractionProfile(session, s_SubActionPaths[i], &state)))
			continue;

		char name[XR_MAX_PATH_LENGTH] = "none";
		uint32_t len = 0;
		if (state.interactionProfile != XR_NULL_PATH)
			xrPathToString(instance, state.interactionProfile, sizeof(name), &len, name);
		EchoXR_Log("%s hand interaction profile: %s", i == ovrHand_Left ? "Left" : "Right", name);
	}
}

bool InputManager::PoseLogVariable()
{
	static const bool enabled = []
	{
		char value[8] = "";
		return GetEnvironmentVariableA("ECHOXR_POSE_LOG", value, sizeof(value)) > 0 && value[0] == '1';
	}();
	return enabled;
}

void InputManager::RecordPose(int hand, XrSpaceVelocityFlags velocityFlags, const ovrPoseStatef& pose, double lead, bool fallback)
{
	using namespace echoxr;
	PoseLogStats& s = m_PoseLogStats[hand];
	s.samples++;
	s.fallbacks += fallback;
	const XrSpaceVelocityFlags both = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
	s.velocityValid += (velocityFlags & both) == both;
	if (lead != 0.0)
	{
		s.leadCount++;
		s.leadSum += lead;
		s.leadMin = std::min(s.leadMin, lead);
		s.leadMax = std::max(s.leadMax, lead);
	}

	auto length = [](Vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); };
	auto minus = [](Vec3 a, Vec3 b) { return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z }; };
	auto vec = [](const ovrVector3f& v) { return Vec3{ v.x, v.y, v.z }; };
	auto quat = [](const ovrQuatf& q) { return Quat{ q.x, q.y, q.z, q.w }; };
	const double dt = pose.TimeInSeconds - s.prev.TimeInSeconds;
	if (s.hasPrev && dt > 0.004 && dt < 0.05)
	{
		// Differentiated velocities, in the tracking space: the orientation change q * prev^-1.
		const Quat q = quat(pose.ThePose.Orientation);
		const Quat p = quat(s.prev.ThePose.Orientation);
		Quat d = Multiply(q, Quat{ -p.x, -p.y, -p.z, p.w });
		if (d.w < 0.0f)
			d = { -d.x, -d.y, -d.z, -d.w };
		const float sinHalf = length(Vec3{ d.x, d.y, d.z });
		const float angle = 2.0f * atan2f(sinHalf, d.w);
		const Vec3 diffAng = sinHalf > 1e-6f ?
			Vec3{ d.x / sinHalf * angle / float(dt), d.y / sinHalf * angle / float(dt), d.z / sinHalf * angle / float(dt) } : Vec3{ 0, 0, 0 };
		const Vec3 cur = vec(pose.ThePose.Position), prev = vec(s.prev.ThePose.Position);
		const Vec3 diffLin = { (cur.x - prev.x) / float(dt), (cur.y - prev.y) / float(dt), (cur.z - prev.z) / float(dt) };

		// Reported: the mean of both samples, as the difference spans the interval between them.
		const Vec3 ra = vec(pose.AngularVelocity), pa = vec(s.prev.AngularVelocity);
		const Vec3 repAng = { (ra.x + pa.x) / 2, (ra.y + pa.y) / 2, (ra.z + pa.z) / 2 };
		const Vec3 rl = vec(pose.LinearVelocity), pl = vec(s.prev.LinearVelocity);
		const Vec3 repLin = { (rl.x + pl.x) / 2, (rl.y + pl.y) / 2, (rl.z + pl.z) / 2 };

		const float diffAngLen = length(diffAng);
		if (diffAngLen > 1.0f)
		{
			s.angCount++;
			s.angRatio += length(repAng) / diffAngLen;
			s.angErrWorld += length(minus(repAng, diffAng)) / diffAngLen;
			s.angErrLocal += length(minus(Rotate(q, repAng), diffAng)) / diffAngLen;
		}
		const float diffLinLen = length(diffLin);
		if (diffLinLen > 0.3f)
		{
			s.linCount++;
			s.linRatio += length(repLin) / diffLinLen;
			s.linErr += length(minus(repLin, diffLin)) / diffLinLen;
		}
	}
	s.prev = pose;
	s.hasPrev = true;
}

void InputManager::LogPoses(XrTime time, const echoxr::ControllerSettings& settings)
{
	const auto now = std::chrono::steady_clock::now();
	if (m_ActionSpaces.size() < 2 * kAimSpaces || now - m_LastPoseLog < std::chrono::seconds(1))
		return;
	m_LastPoseLog = now;

	// One line per hand: where a pose sits in the grip pose's frame, as an angle about an axis
	// plus an offset in centimetres. Flags: the location's orientation/position valid bits.
	auto describe = [](const echoxr::Pose& p, unsigned flags, char* out, size_t size)
	{
		float degrees;
		echoxr::Vec3 axis;
		echoxr::ToAxisAngle(p.orientation, degrees, axis);
		snprintf(out, size, "%.1f deg about (%.2f %.2f %.2f), at (%.1f %.1f %.1f) cm, flags %x",
			degrees, axis.x, axis.y, axis.z, p.position.x * 100.0f, p.position.y * 100.0f, p.position.z * 100.0f, flags);
	};
	const unsigned valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
	for (int i = 0; i < ovrHand_Count; i++)
	{
		// The aim pose as the runtime puts it against the grip pose, and the hand Echo gets.
		XrSpaceLocation location = XR_TYPE(SPACE_LOCATION);
		if (XR_FAILED(xrLocateSpace(m_ActionSpaces[kAimSpaces + i], m_ActionSpaces[i], time, &location)))
			location.locationFlags = 0;
		const echoxr::Pose aimInGrip = FromXr(location.pose);
		const echoxr::Pose user = echoxr::UserHandOffset(settings.pitch, settings.yaw, settings.roll,
			settings.offsetX, settings.offsetY, settings.offsetZ, i);
		const echoxr::Pose handInGrip = echoxr::Compose(
			settings.handPose == echoxr::HandPose::Aim ? aimInGrip : echoxr::kGripToOvrHand, user);
		char aim[160], hand[160];
		describe(aimInGrip, (unsigned)(location.locationFlags & valid), aim, sizeof(aim));
		describe(handInGrip, (unsigned)(location.locationFlags & valid), hand, sizeof(hand));
		EchoXR_Log("Pose %s, from grip: aim %s; hand %s", i == ovrHand_Left ? "left" : "right", aim, hand);

		PoseLogStats& s = m_PoseLogStats[i];
		if (s.samples)
		{
			char lead[96] = "no time given";
			if (s.leadCount)
				snprintf(lead, sizeof(lead), "%.1f ms ahead (%.1f to %.1f)", s.leadSum / s.leadCount * 1000.0, s.leadMin * 1000.0, s.leadMax * 1000.0);
			EchoXR_Log("Pose %s, motion: %d poses, velocities valid %d, %s, %d frame-time fallbacks; "
				"turning %d: reported/differentiated %.2f, error world %.2f local %.2f; moving %d: ratio %.2f, error %.2f",
				i == ovrHand_Left ? "left" : "right", s.samples, s.velocityValid, lead, s.fallbacks,
				s.angCount, s.angCount ? s.angRatio / s.angCount : 0.0, s.angCount ? s.angErrWorld / s.angCount : 0.0,
				s.angCount ? s.angErrLocal / s.angCount : 0.0,
				s.linCount, s.linCount ? s.linRatio / s.linCount : 0.0, s.linCount ? s.linErr / s.linCount : 0.0);
		}
		const bool hasPrev = s.hasPrev;
		const ovrPoseStatef prev = s.prev;
		s = PoseLogStats();
		s.hasPrev = hasPrev;
		s.prev = prev;
	}
}

InputManager::~InputManager()
{
	for (InputDevice* device : m_InputDevices)
		delete device;
}

ovrResult InputManager::SetControllerVibration(ovrSession session, ovrControllerType controllerType, float frequency, float amplitude)
{
	std::lock_guard<std::mutex> lock(m_ActionMutex);

	// Clamp the input
	frequency = std::min(std::max(frequency, 0.0f), 1.0f);
	amplitude = std::min(std::max(amplitude, 0.0f), 1.0f);

	for (InputDevice* device : m_InputDevices)
	{
		if (controllerType & device->GetType() && device->IsConnected())
			CHK_OVR(device->SetVibration(session->Session, controllerType, frequency, amplitude));
	}

	return ovrSuccess;
}

ovrResult InputManager::GetInputState(ovrSession session, ovrControllerType controllerType, ovrInputState* inputState)
{
	std::lock_guard<std::mutex> lock(m_ActionMutex);
	memset(inputState, 0, sizeof(ovrInputState));
	if (session->Session)
		SyncIfStale(session->Session);

	if (controllerType == ovrControllerType_Active)
		controllerType = ovrControllerType_Touch;

	for (InputDevice* device : m_InputDevices)
	{
		ovrControllerType type = device->GetType();
		if (controllerType & type && device->IsConnected())
			device->GetInputState(session->Session, controllerType, inputState);
	}

	inputState->TimeInSeconds = ovr_GetTimeInSeconds();
	inputState->ControllerType = controllerType;
	return ovrSuccess;
}

ovrResult InputManager::SubmitControllerVibration(ovrSession session, ovrControllerType controllerType, const ovrHapticsBuffer* buffer)
{
	std::lock_guard<std::mutex> lock(m_ActionMutex);

	for (InputDevice* device : m_InputDevices)
	{
		if (controllerType & device->GetType() && device->IsConnected())
			device->SubmitVibration(controllerType, buffer);
	}

	return ovrSuccess;
}

ovrResult InputManager::GetControllerVibrationState(ovrSession session, ovrControllerType controllerType, ovrHapticsPlaybackState* outState)
{
	std::lock_guard<std::mutex> lock(m_ActionMutex);

	memset(outState, 0, sizeof(ovrHapticsPlaybackState));

	for (InputDevice* device : m_InputDevices)
	{
		if (controllerType & device->GetType() && device->IsConnected())
			device->GetVibrationState(controllerType & ovrControllerType_RTouch ? ovrHand_Right : ovrHand_Left, outState);
	}

	return ovrSuccess;
}

unsigned int InputManager::SpaceRelationToPoseState(const Located& location, double time, ovrPoseStatef& lastPoseState, ovrPoseStatef& outPoseState)
{
	unsigned int flags = 0;
	const echoxr::PoseMotion& m = location.motion;

	if (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
	{
		outPoseState.ThePose.Orientation = OVR::Quatf(m.pose.orientation.x, m.pose.orientation.y, m.pose.orientation.z, m.pose.orientation.w);
		flags |= ovrStatus_OrientationValid;
		flags |= (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) ? ovrStatus_OrientationTracked : 0;
	}
	else
	{
		outPoseState.ThePose.Orientation = XR::Quatf::Identity();
	}

	if (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
	{
		outPoseState.ThePose.Position = OVR::Vector3f(m.pose.position.x, m.pose.position.y, m.pose.position.z);
		flags |= ovrStatus_PositionValid;
		flags |= (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) ? ovrStatus_PositionTracked : 0;
	}
	else
	{
		outPoseState.ThePose.Position = XR::Vector3f::Zero();
	}

	if (location.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
	{
		OVR::Vector3f currav(m.angular.x, m.angular.y, m.angular.z);
		XR::Vector3f lastav(lastPoseState.AngularVelocity);
		outPoseState.AngularVelocity = currav;
		outPoseState.AngularAcceleration = time > lastPoseState.TimeInSeconds ?
			(currav - lastav) / float(time - lastPoseState.TimeInSeconds) : OVR::Vector3f(lastPoseState.AngularAcceleration);
	}
	else
	{
		outPoseState.AngularVelocity = XR::Vector3f::Zero();
		outPoseState.AngularAcceleration = XR::Vector3f::Zero();
	}

	if (location.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
	{
		OVR::Vector3f currlv(m.linear.x, m.linear.y, m.linear.z);
		XR::Vector3f lastlv(lastPoseState.LinearVelocity);
		outPoseState.LinearVelocity = currlv;
		outPoseState.LinearAcceleration = time > lastPoseState.TimeInSeconds ?
			(currlv - lastlv) / float(time - lastPoseState.TimeInSeconds) : OVR::Vector3f(lastPoseState.LinearAcceleration);
	}
	else
	{
		outPoseState.LinearVelocity = XR::Vector3f::Zero();
		outPoseState.LinearAcceleration = XR::Vector3f::Zero();
	}

	outPoseState.TimeInSeconds = time;

	return flags;
}

InputManager::Located InputManager::LocateInTracking(ovrSession session, XrSpace space, XrTime time)
{
	// EchoXR: located against the plain reference space and turned into the tracking space
	// (the recentered one) here. A runtime should rotate velocities into a base space that has
	// a pose of its own, but Virtual Desktop's doesn't: the velocities then missed the
	// recenter's turn, and throws went off by it.
	const ovrTrackingOrigin origin = session->TrackingOrigin;
	XrSpace base = session->OriginSpaces[origin];
	XrSpaceVelocity velocity = XR_TYPE(SPACE_VELOCITY);
	XrSpaceLocation location = XR_TYPE(SPACE_LOCATION);
	location.next = &velocity;

	Located out;
	if (XR_FAILED(xrLocateSpace(space, base, time, &location)))
	{
		out.fallback = true;
		location = XR_TYPE(SPACE_LOCATION);
		location.next = &velocity;
		velocity = XR_TYPE(SPACE_VELOCITY);
		if (XR_FAILED(xrLocateSpace(space, base, (*session->CurrentFrame).predictedDisplayTime, &location)))
			return out;
	}

	echoxr::PoseMotion m = { FromXr(location.pose), { 0, 0, 0 }, { 0, 0, 0 } };
	if (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
		m.linear = FromXr(velocity.linearVelocity);
	if (velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
		m.angular = FromXr(velocity.angularVelocity);
	out.motion = echoxr::ToSpace(session->TrackingPoses[origin], m);
	out.locationFlags = location.locationFlags;
	out.velocityFlags = velocity.velocityFlags;
	return out;
}

bool InputManager::LocateHand(ovrSession session, int hand, XrTime time, const echoxr::ControllerSettings& settings,
	Located& out, echoxr::PoseMotion* beforeOffset) const
{
	if (m_ActionSpaces.size() < 2 * kAimSpaces)
		return false;

	// The grip pose moved to LibOVR's controller origin, as a point on the controller (its
	// velocity includes the turn), or the aim pose as it is.
	const bool aim = settings.handPose == echoxr::HandPose::Aim;
	out = LocateInTracking(session, m_ActionSpaces[(aim ? kAimSpaces : 0) + hand], time);
	if (!aim)
		out.motion = echoxr::Attach(out.motion, echoxr::kGripToOvrHand, true);
	if (beforeOffset)
		*beforeOffset = out.motion;

	// The player's turn and offset move the hand Echo sees, never its speed: the velocities
	// stay those of the hand above, so no setting makes a throw or a push stronger.
	const echoxr::Pose user = echoxr::UserHandOffset(settings.pitch, settings.yaw, settings.roll,
		settings.offsetX, settings.offsetY, settings.offsetZ, hand);
	out.motion = echoxr::Attach(out.motion, user, false);
	return true;
}

void InputManager::GetTrackingState(ovrSession session, ovrTrackingState* outState, double absTime)
{
	double calledTime = absTime;

	if (!session->Session)
		return;
	std::lock_guard<std::mutex> lock(m_ActionMutex);
	SyncIfStale(session->Session);
	const echoxr::ControllerSettings settings = EchoXR_Controls();
	const bool log = PoseLogVariable() || settings.poseLog;

	if (absTime <= 0.0)
		absTime = ovr_GetTimeInSeconds();

	XrTime displayTime = AbsTimeToXrTime(session->Instance, absTime);
	XrSpace space = session->TrackingSpaces[session->TrackingOrigin];

	// Get the head space location
	const Located head = LocateInTracking(session, session->ViewSpace, displayTime);
	outState->StatusFlags = SpaceRelationToPoseState(head, absTime, m_LastTrackingState.HeadPose, outState->HeadPose);

	// Get the hand space locations
	for (int i = 0; i < ovrHand_Count; i++)
	{
		Located hand;
		echoxr::PoseMotion beforeOffset;
		if (!LocateHand(session, i, displayTime, settings, hand, &beforeOffset))
			break;
		outState->HandStatusFlags[i] = SpaceRelationToPoseState(hand, absTime, m_LastTrackingState.HandPoses[i], outState->HandPoses[i]);
		if (log && (outState->HandStatusFlags[i] & ovrStatus_PositionValid))
		{
			// The log checks the velocities against the poses, before the player's offset (which
			// moves the pose and deliberately not the velocity).
			Located plain = hand;
			plain.motion = beforeOffset;
			ovrPoseStatef unused = m_LastTrackingState.HandPoses[i], pose;
			SpaceRelationToPoseState(plain, absTime, unused, pose);
			RecordPose(i, hand.velocityFlags, pose, calledTime > 0.0 ? calledTime - ovr_GetTimeInSeconds() : 0.0, hand.fallback);
		}
	}

	XrSpace origin = session->OriginSpaces[session->TrackingOrigin];
	XrSpaceLocation location = XR_TYPE(SPACE_LOCATION);
	if (XR_FAILED(xrLocateSpace(space, origin, displayTime, &location)))
		xrLocateSpace(space, origin, (*session->CurrentFrame).predictedDisplayTime, &location);

	if (location.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT))
		outState->CalibratedOrigin = XR::Posef(location.pose);
	else
		outState->CalibratedOrigin = OVR::Posef::Identity();

	if (log)
		LogPoses(displayTime, settings);

	m_LastTrackingState = *outState;
}

ovrResult InputManager::GetDevicePoses(ovrSession session, ovrTrackedDeviceType* deviceTypes, int deviceCount, double absTime, ovrPoseStatef* outDevicePoses)
{
	std::lock_guard<std::mutex> lock(m_ActionMutex);
	if (session->Session)
		SyncIfStale(session->Session);
	const echoxr::ControllerSettings settings = EchoXR_Controls();

	if (absTime <= 0.0)
		absTime = ovr_GetTimeInSeconds();

	XrTime displayTime = AbsTimeToXrTime(session->Instance, absTime);
	for (int i = 0; i < deviceCount; i++)
	{
		// Locate the device types we recognize
		Located located;
		ovrPoseStatef* pLastState = nullptr;
		switch (deviceTypes[i])
		{
			case ovrTrackedDevice_HMD:
				located = LocateInTracking(session, session->ViewSpace, displayTime);
				pLastState = &m_LastTrackingState.HeadPose;
				break;
			case ovrTrackedDevice_LTouch:
			case ovrTrackedDevice_RTouch:
			{
				const int hand = deviceTypes[i] == ovrTrackedDevice_LTouch ? ovrHand_Left : ovrHand_Right;
				if (LocateHand(session, hand, displayTime, settings, located))
					pLastState = &m_LastTrackingState.HandPoses[hand];
				break;
			}
		}

		ovrPoseStatef none = {};
		SpaceRelationToPoseState(located, absTime, pLastState ? *pLastState : none, outDevicePoses[i]);
	}

	return ovrSuccess;
}

/* Action child-class */

InputManager::Action::Action(InputDevice* device, XrActionType type, const char* actionName, const char* localizedName, bool handedAction)
	: m_IsHanded(handedAction)
	, m_Action(XR_NULL_HANDLE)
{
	XrActionCreateInfo createInfo = XR_TYPE(ACTION_CREATE_INFO);
	createInfo.actionType = type;
	strcpy_s(createInfo.actionName, actionName);
	strcpy_s(createInfo.localizedActionName, localizedName);
	if (handedAction)
	{
		createInfo.countSubactionPaths = ovrHand_Count;
		createInfo.subactionPaths = s_SubActionPaths;
	}
	XrResult rs = xrCreateAction(device->ActionSet(), &createInfo, &m_Action);
	assert(XR_SUCCEEDED(rs));
}

bool InputManager::Action::GetDigital(XrSession session, ovrHandType hand) const
{
	XrActionStateGetInfo info = XR_TYPE(ACTION_STATE_GET_INFO);
	info.action = m_Action;
	info.subactionPath = m_IsHanded ? s_SubActionPaths[hand] : XR_NULL_PATH;
	XrActionStateBoolean data = XR_TYPE(ACTION_STATE_BOOLEAN);
	XrResult rs = xrGetActionStateBoolean(session, &info, &data);
	assert(XR_SUCCEEDED(rs));
	return XR_SUCCEEDED(rs) && data.isActive && data.currentState;
}

bool InputManager::Action::IsPressed(XrSession session, ovrHandType hand) const
{
	XrActionStateGetInfo info = XR_TYPE(ACTION_STATE_GET_INFO);
	info.action = m_Action;
	info.subactionPath = m_IsHanded ? s_SubActionPaths[hand] : XR_NULL_PATH;
	XrActionStateBoolean data = XR_TYPE(ACTION_STATE_BOOLEAN);
	XrResult rs = xrGetActionStateBoolean(session, &info, &data);
	assert(XR_SUCCEEDED(rs));
	return data.changedSinceLastSync && data.currentState;
}

bool InputManager::Action::IsReleased(XrSession session, ovrHandType hand) const
{
	XrActionStateGetInfo info = XR_TYPE(ACTION_STATE_GET_INFO);
	info.action = m_Action;
	info.subactionPath = m_IsHanded ? s_SubActionPaths[hand] : XR_NULL_PATH;
	XrActionStateBoolean data = XR_TYPE(ACTION_STATE_BOOLEAN);
	XrResult rs = xrGetActionStateBoolean(session, &info, &data);
	assert(XR_SUCCEEDED(rs));
	return data.changedSinceLastSync && !data.currentState;
}

float InputManager::Action::GetAnalog(XrSession session, ovrHandType hand) const
{
	XrActionStateGetInfo info = XR_TYPE(ACTION_STATE_GET_INFO);
	info.action = m_Action;
	info.subactionPath = m_IsHanded ? s_SubActionPaths[hand] : XR_NULL_PATH;
	XrActionStateFloat data = XR_TYPE(ACTION_STATE_FLOAT);
	XrResult rs = xrGetActionStateFloat(session, &info, &data);
	assert(XR_SUCCEEDED(rs));
	return data.currentState;
}

ovrVector2f InputManager::Action::GetVector(XrSession session, ovrHandType hand) const
{
	XrActionStateGetInfo info = XR_TYPE(ACTION_STATE_GET_INFO);
	info.action = m_Action;
	info.subactionPath = m_IsHanded ? s_SubActionPaths[hand] : XR_NULL_PATH;
	XrActionStateVector2f data = XR_TYPE(ACTION_STATE_VECTOR2F);
	XrResult rs = xrGetActionStateVector2f(session, &info, &data);
	assert(XR_SUCCEEDED(rs));
	return XR::Vector2f(data.currentState);
}

XrSpace InputManager::Action::CreateSpace(XrSession session, ovrHandType hand, const XrPosef& poseInActionSpace) const
{
	XrSpace space;
	XrActionSpaceCreateInfo spaceInfo = XR_TYPE(ACTION_SPACE_CREATE_INFO);
	spaceInfo.action = m_Action;
	spaceInfo.subactionPath = m_IsHanded ? s_SubActionPaths[hand] : XR_NULL_PATH;
	spaceInfo.poseInActionSpace = poseInActionSpace;
	XrResult rs = xrCreateActionSpace(session, &spaceInfo, &space);
	assert(XR_SUCCEEDED(rs));
	return space;
}

/* Controller child-classes */

InputManager::InputDevice::InputDevice(XrInstance instance, const char* actionSetName, const char* localizedName)
{
	XrActionSetCreateInfo createInfo = XR_TYPE(ACTION_SET_CREATE_INFO);
	strcpy_s(createInfo.actionSetName, actionSetName);
	strcpy_s(createInfo.localizedActionSetName, localizedName);
	XrResult rs = xrCreateActionSet(instance, &createInfo, &m_ActionSet);
	assert(XR_SUCCEEDED(rs));
}

InputManager::InputDevice::~InputDevice()
{
	XrResult rs = xrDestroyActionSet(m_ActionSet);
	assert(XR_SUCCEEDED(rs));
}

ovrVector2f InputManager::InputDevice::ApplyDeadzone(ovrVector2f axis, float deadZone)
{
	XR::Vector2f vector(axis);
	float mag = vector.Length();
	if (mag > deadZone)
	{
		// scale such that output magnitude is in the range[0, 1]
		float legalRange = 1.0f - deadZone;
		float normalizedMag = std::min(1.0f, (mag - deadZone) / legalRange);
		float scale = normalizedMag / mag;
		return vector * scale;
	}
	else
	{
		// stick is in the inner dead zone
		return OVR::Vector2f();
	}
}

ovrButton InputManager::InputDevice::TrackpadToDPad(ovrVector2f trackpad)
{
	if (trackpad.y < trackpad.x) {
		if (trackpad.y < -trackpad.x)
			return ovrButton_Down;
		else
			return ovrButton_Right;
	}
	else {
		if (trackpad.y < -trackpad.x)
			return ovrButton_Left;
		else
			return ovrButton_Up;
	}
}

InputManager::OculusTouch::OculusTouch(XrInstance instance)
	: InputDevice(instance, "touch", "Oculus Touch")
	, m_Button_Enter(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "enter-click", "Menu button")
	, m_Button_Home(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "home-click", "Home button")
	, m_Button_AX(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "ax-click", "A/X pressed", true)
	, m_Button_BY(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "by-click", "B/Y pressed", true)
	, m_Button_Thumb(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb-click", "Thumbstick pressed", true)
	, m_Touch_AX(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "ax-touch", "A/X touched", true)
	, m_Touch_BY(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "by-touch", "B/Y touched", true)
	, m_Touch_Thumb(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb-touch", "Thumbstick touched", true)
	, m_Touch_ThumbRest(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "rest-touch", "Thumb rest touched", true)
	, m_Touch_IndexTrigger(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger-touch", "Trigger touched", true)
	, m_IndexTrigger(this, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger button", true)
	, m_HandTrigger(this, XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grip button", true)
	, m_Thumbstick(this, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", true)
	, m_GripPose(this, XR_ACTION_TYPE_POSE_INPUT, "grip-pose", "Controller grip pose", true)
	, m_AimPose(this, XR_ACTION_TYPE_POSE_INPUT, "aim-pose", "Controller aim pose", true)
	, m_Vibration(this, XR_ACTION_TYPE_VIBRATION_OUTPUT, "vibration", "Vibration", true)
{
	if (Runtime::Get().UseHack(Runtime::HACK_WMR_PROFILE))
		m_Trackpad_Buttons = Action(this, XR_ACTION_TYPE_FLOAT_INPUT, "trackpad-buttons", "Trackpad Buttons", true);
}

InputManager::OculusTouch::~OculusTouch()
{
}

void InputManager::OculusTouch::UpdateHaptics(XrSession session, XrDuration displayPeriod)
{
	const float strength = EchoXR_Controls().vibration;
	for (int i = 0; i < ovrHand_Count; i++)
	{
		float amplitude = echoxr::ScaleVibration(m_HapticsBuffer[i].GetSample(), strength);
		if (amplitude > 0.0f)
		{
			XrHapticActionInfo info = XR_TYPE(HAPTIC_ACTION_INFO);
			info.action = m_Vibration;
			info.subactionPath = s_SubActionPaths[i];
			XrHapticVibration vibration = XR_TYPE(HAPTIC_VIBRATION);
			vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
			vibration.amplitude = amplitude;
			vibration.duration = displayPeriod;
			XrResult rs = xrApplyHapticFeedback(session, &info, (XrHapticBaseHeader*)&vibration);
			assert(XR_SUCCEEDED(rs));
		}
	}
}

void InputManager::OculusTouch::GetSuggestedBindings(std::vector<ProfileBindings>& outProfiles) const
{
	if (Runtime::Get().UseHack(Runtime::HACK_WMR_PROFILE))
	{
		outProfiles.push_back(GetLayoutBindings(Layout_WMR));
		return;
	}

	// Always offer the Touch profile: it's the one nearly every OpenXR game
	// suggests, so new controllers (such as the Steam Frame's) ship bindings
	// for it even when the runtime can't convert the Index profile to them.
	outProfiles.push_back(GetLayoutBindings(Layout_Touch));
	if (Runtime::Get().UseHack(Runtime::HACK_VALVE_INDEX_PROFILE))
		outProfiles.push_back(GetLayoutBindings(Layout_Index));
	// EchoXR: Monado doesn't convert the Touch profile to WMR controllers (its simulated ones
	// included), which then get no binding at all; offering WMR's own costs nothing elsewhere.
	outProfiles.push_back(GetLayoutBindings(Layout_WMR));
}

InputManager::ProfileBindings InputManager::OculusTouch::GetLayoutBindings(Layout layout) const
{
	std::string prefixes[ovrHand_Count] = { "/user/hand/left", "/user/hand/right" };
	ProfileBindings result;
	std::vector<XrActionSuggestedBinding>& outBindings = result.Bindings;

#define ADD_BINDING(action, path) outBindings.push_back(XrActionSuggestedBinding{ action, GetXrPath(path) })

	if (layout == Layout_WMR)
	{
		ADD_BINDING(m_Button_Enter, "/user/hand/left/input/menu/click");
		ADD_BINDING(m_Button_Home, "/user/hand/right/input/menu/click");
	}
	else
	{
		if (layout == Layout_Index)
		{
			ADD_BINDING(m_Button_Enter, "/user/hand/left/input/trackpad/force");
			ADD_BINDING(m_Button_AX, "/user/hand/left/input/a/click");
			ADD_BINDING(m_Button_BY, "/user/hand/left/input/b/click");
			ADD_BINDING(m_Touch_AX, "/user/hand/left/input/a/touch");
			ADD_BINDING(m_Touch_BY, "/user/hand/left/input/b/touch");
			ADD_BINDING(m_Button_Home, "/user/hand/right/input/trackpad/force");
		}
		else
		{
			ADD_BINDING(m_Button_Enter, "/user/hand/left/input/menu/click");
			ADD_BINDING(m_Button_AX, "/user/hand/left/input/x/click");
			ADD_BINDING(m_Button_BY, "/user/hand/left/input/y/click");
			ADD_BINDING(m_Touch_AX, "/user/hand/left/input/x/touch");
			ADD_BINDING(m_Touch_BY, "/user/hand/left/input/y/touch");
			ADD_BINDING(m_Button_Home, "/user/hand/right/input/system/click");
		}
		ADD_BINDING(m_Button_AX, "/user/hand/right/input/a/click");
		ADD_BINDING(m_Button_BY, "/user/hand/right/input/b/click");
		ADD_BINDING(m_Touch_AX, "/user/hand/right/input/a/touch");
		ADD_BINDING(m_Touch_BY, "/user/hand/right/input/b/touch");
	}

	for (int i = 0; i < ovrHand_Count; i++)
	{
		if (layout == Layout_WMR)
		{
			// EchoXR: only made on the WMR runtime; a null action makes the runtime reject the profile.
			if ((XrAction)m_Trackpad_Buttons != XR_NULL_HANDLE)
				ADD_BINDING(m_Trackpad_Buttons, prefixes[i] + "/input/trackpad/y");
			ADD_BINDING(m_Button_AX, prefixes[i] + "/input/trackpad/click");
			ADD_BINDING(m_Button_BY, prefixes[i] + "/input/trackpad/click");
			ADD_BINDING(m_Touch_AX, prefixes[i] + "/input/trackpad/touch");
			ADD_BINDING(m_Touch_BY, prefixes[i] + "/input/trackpad/touch");

			ADD_BINDING(m_Thumbstick, prefixes[i] + "/input/thumbstick");
			ADD_BINDING(m_Button_Thumb, prefixes[i] + "/input/thumbstick/click");
			ADD_BINDING(m_HandTrigger, prefixes[i] + "/input/squeeze/click");
			ADD_BINDING(m_Touch_IndexTrigger, prefixes[i] + "/input/trigger/value");
			ADD_BINDING(m_IndexTrigger, prefixes[i] + "/input/trigger/value");
		}
		else
		{
			ADD_BINDING(m_Thumbstick, prefixes[i] + "/input/thumbstick");
			ADD_BINDING(m_Button_Thumb, prefixes[i] + "/input/thumbstick/click");
			ADD_BINDING(m_Touch_Thumb, prefixes[i] + "/input/thumbstick/touch");
			if (layout == Layout_Index)
				ADD_BINDING(m_Touch_ThumbRest, prefixes[i] + "/input/trackpad/touch");
			else
				ADD_BINDING(m_Touch_ThumbRest, prefixes[i] + "/input/thumbrest/touch");
			ADD_BINDING(m_HandTrigger, prefixes[i] + "/input/squeeze/value");
			ADD_BINDING(m_Touch_IndexTrigger, prefixes[i] + "/input/trigger/touch");
			ADD_BINDING(m_IndexTrigger, prefixes[i] + "/input/trigger/value");
		}

		// EchoXR: both poses. The grip pose becomes LibOVR's controller pose (kGripToOvrHand),
		// the same on every runtime; the aim pose, Revive's choice, is there for HandPose = aim.
		ADD_BINDING(m_GripPose, prefixes[i] + "/input/grip/pose");
		ADD_BINDING(m_AimPose, prefixes[i] + "/input/aim/pose");
		ADD_BINDING(m_Vibration, prefixes[i] + "/output/haptic");
	}

#undef ADD_BINDING

	if (layout == Layout_Index)
		result.Profile = GetXrPath("/interaction_profiles/valve/index_controller");
	else if (layout == Layout_WMR)
		result.Profile = GetXrPath("/interaction_profiles/microsoft/motion_controller");
	else
		result.Profile = GetXrPath("/interaction_profiles/oculus/touch_controller");
	return result;
}

void InputManager::OculusTouch::GetActionSpaces(XrSession session, std::vector<XrSpace>& outSpaces) const
{
	// Grip left/right, then aim left/right (InputManager::kAimSpaces).
	outSpaces.push_back(m_GripPose.CreateSpace(session, ovrHand_Left));
	outSpaces.push_back(m_GripPose.CreateSpace(session, ovrHand_Right));
	outSpaces.push_back(m_AimPose.CreateSpace(session, ovrHand_Left));
	outSpaces.push_back(m_AimPose.CreateSpace(session, ovrHand_Right));
}

void InputManager::OculusTouch::GetActiveSets(std::vector<XrActiveActionSet>& outSets) const
{
	outSets.push_back(XrActiveActionSet{ m_ActionSet, s_SubActionPaths[ovrHand_Left] });
	outSets.push_back(XrActiveActionSet{ m_ActionSet, s_SubActionPaths[ovrHand_Right] });
}

ovrControllerType InputManager::OculusTouch::GetType() const
{
	return ovrControllerType_Touch;
}

bool InputManager::OculusTouch::IsConnected() const
{
	return true;
}

void InputManager::OculusTouch::GetInputState(XrSession session, ovrControllerType controllerType, ovrInputState* inputState)
{
	const echoxr::ControllerSettings settings = EchoXR_Controls();

	if (m_Button_Enter.GetDigital(session))
		inputState->Buttons |= ovrButton_Enter;

	// In most games this doesn't really do anything,
	// because it would normally open the dashboard.
	if (m_Button_Home.GetDigital(session))
		inputState->Buttons |= ovrButton_Home;

	for (int i = 0; i < ovrHand_Count; i++)
	{
		ovrHandType hand = (ovrHandType)i;
		unsigned int buttons = 0, touches = 0;

		if (Runtime::Get().UseHack(Runtime::HACK_WMR_PROFILE))
		{
			if (m_Button_AX.GetDigital(session, hand) || m_Button_BY.GetDigital(session, hand))
			{
				if (m_Trackpad_Buttons.GetAnalog(session, hand) > 0.0f)
					buttons |= ovrButton_B;
				else
					buttons |= ovrButton_A;
			}

			if (m_Touch_AX.GetDigital(session, hand) || m_Touch_BY.GetDigital(session, hand))
			{
				if (m_Trackpad_Buttons.GetAnalog(session, hand) > 0.0f)
					touches |= ovrTouch_B;
				else
					touches |= ovrTouch_A;
			}
		}
		else
		{
			if (m_Button_AX.GetDigital(session, hand))
				buttons |= ovrButton_A;

			if (m_Touch_AX.GetDigital(session, hand))
				touches |= ovrTouch_A;

			if (m_Button_BY.GetDigital(session, hand))
				buttons |= ovrButton_B;

			if (m_Touch_BY.GetDigital(session, hand))
				touches |= ovrTouch_B;
		}

		if (m_Button_Thumb.GetDigital(session, hand))
			buttons |= ovrButton_RThumb;

		if (m_Touch_Thumb.GetDigital(session, hand))
			touches |= ovrTouch_RThumb;

		if (m_Touch_ThumbRest.GetDigital(session, hand))
			touches |= ovrTouch_RThumbRest;

		if (m_Touch_IndexTrigger.GetDigital(session, hand))
			touches |= ovrTouch_RIndexTrigger;

		inputState->ThumbstickNoDeadzone[i] = m_Thumbstick.GetVector(session, hand);
		inputState->IndexTriggerNoDeadzone[i] = m_IndexTrigger.GetAnalog(session, hand);
		inputState->HandTriggerNoDeadzone[i] = m_HandTrigger.GetAnalog(session, hand);

		// Derive gestures from touch flags
		if (!Runtime::Get().UseHack(Runtime::HACK_WMR_PROFILE) || inputState->HandTriggerNoDeadzone[i] > 0.5f)
		{
			if (!(touches & ovrTouch_RIndexTrigger))
				touches |= ovrTouch_RIndexPointing;

			if (!(touches & (ovrTouch_A | ovrTouch_B | ovrTouch_RThumb | ovrTouch_RThumbRest)))
				touches |= ovrTouch_RThumbUp;
		}

		// Apply deadzones where needed
		inputState->Thumbstick[i] = ApplyDeadzone(inputState->ThumbstickNoDeadzone[i], settings.thumbstickDeadzone);
		inputState->IndexTrigger[i] = echoxr::ApplyTriggerDeadzone(inputState->IndexTriggerNoDeadzone[i], settings.triggerDeadzone);
		inputState->HandTrigger[i] = echoxr::ApplyTriggerDeadzone(inputState->HandTriggerNoDeadzone[i], settings.gripDeadzone);

		// We have no way to get raw values
		inputState->ThumbstickRaw[i] = inputState->ThumbstickNoDeadzone[i];
		inputState->IndexTriggerRaw[i] = inputState->IndexTriggerNoDeadzone[i];
		inputState->HandTriggerRaw[i] = inputState->HandTriggerNoDeadzone[i];

		inputState->Buttons |= (hand == ovrHand_Left) ? buttons << 8 : buttons;
		inputState->Touches |= (hand == ovrHand_Left) ? touches << 8 : touches;
	}

	// EchoXR: Menu from a chord, for runtimes that keep the Menu button to themselves.
	inputState->Buttons = echoxr::ApplyMenuChord(inputState->Buttons, settings.menuChord);
}

ovrResult InputManager::OculusTouch::SetVibration(XrSession session, ovrControllerType controllerType, float frequency, float amplitude)
{
	std::vector<XrPath> subPaths;
	if (controllerType & ovrControllerType_LTouch)
		subPaths.push_back(s_SubActionPaths[ovrHand_Left]);
	if (controllerType & ovrControllerType_RTouch)
		subPaths.push_back(s_SubActionPaths[ovrHand_Right]);

	// EchoXR: at the player's vibration strength (EchoXR.txt).
	amplitude = echoxr::ScaleVibration(amplitude, EchoXR_Controls().vibration);
	for (XrPath path : subPaths)
	{
		XrHapticActionInfo info = XR_TYPE(HAPTIC_ACTION_INFO);
		info.action = m_Vibration;
		info.subactionPath = path;
		if (frequency > 0.0f && amplitude > 0.0f)
		{
			XrHapticVibration vibration = XR_TYPE(HAPTIC_VIBRATION);
			vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
			vibration.amplitude = amplitude;
			vibration.duration = Runtime::Get().UseHack(Runtime::HACK_MIN_HAPTIC_DURATION) ?
				XR_MIN_HAPTIC_DURATION : MAX_VIBRATION_DURATION;
			CHK_XR(xrApplyHapticFeedback(session, &info, (XrHapticBaseHeader*)&vibration));
		}
		else
		{
			CHK_XR(xrStopHapticFeedback(session, &info));
		}
	}
	return ovrSuccess;
}

void InputManager::OculusTouch::SubmitVibration(ovrControllerType controllerType, const ovrHapticsBuffer* buffer)
{
	if (controllerType & ovrControllerType_LTouch)
		m_HapticsBuffer[ovrHand_Left].AddSamples(buffer);
	if (controllerType & ovrControllerType_RTouch)
		m_HapticsBuffer[ovrHand_Right].AddSamples(buffer);
}

InputManager::OculusRemote::OculusRemote(XrInstance instance)
	: InputDevice(instance, "remote", "Oculus Remote")
	, m_IsConnected(false)
	, m_Toggle_Connected(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "toggle-connect", "Connect the remote")
	, m_Button_Up(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "up-click", "Up pressed")
	, m_Button_Down(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "down-click", "Down pressed")
	, m_Button_Left(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "left-click", "Left pressed")
	, m_Button_Right(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "right-click", "Right pressed")
	, m_Button_Enter(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "enter-click", "Select pressed")
	, m_Button_Back(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "back-click", "Back pressed")
	, m_Button_VolUp(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "vol-up", "Volume up")
	, m_Button_VolDown(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "vol-down", "Volume down")
{
}

bool InputManager::OculusRemote::IsConnected() const
{
	return m_IsConnected;
}

void InputManager::OculusRemote::GetInputState(XrSession session, ovrControllerType controllerType, ovrInputState* inputState)
{
	unsigned int buttons = 0;

	// Allow the user to enable/disable the remote
	if (m_Toggle_Connected.IsPressed(session))
		m_IsConnected = !m_IsConnected;

	if (m_Button_Up.GetDigital(session))
		buttons |= ovrButton_Up;

	if (m_Button_Down.GetDigital(session))
		buttons |= ovrButton_Down;

	if (m_Button_Left.GetDigital(session))
		buttons |= ovrButton_Left;

	if (m_Button_Right.GetDigital(session))
		buttons |= ovrButton_Right;

	if (m_Button_Enter.GetDigital(session))
		buttons |= ovrButton_Enter;

	if (m_Button_Back.GetDigital(session))
		buttons |= ovrButton_Back;

	if (m_Button_VolUp.GetDigital(session))
		buttons |= ovrButton_VolUp;

	if (m_Button_VolDown.GetDigital(session))
		buttons |= ovrButton_VolDown;

	inputState->Buttons |= buttons;
}

void InputManager::OculusRemote::GetActiveSets(std::vector<XrActiveActionSet>& outSets) const
{
	XrActiveActionSet active = { m_ActionSet };
	outSets.push_back(active);
}

InputManager::XboxGamepad::XboxGamepad(XrInstance instance)
	: InputDevice(instance, "xbox", "Xbox Controller")
	, m_Button_A(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "a-click", "A pressed")
	, m_Button_B(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "b-click", "B pressed")
	, m_Button_RThumb(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "rthumb-click", "Right thumbstick pressed")
	, m_Button_RShoulder(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "rshoulder-click", "Right shoulder pressed")
	, m_Button_X(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "x-click", "X pressed")
	, m_Button_Y(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "y-click", "Y pressed")
	, m_Button_LThumb(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "lthumb-click", "Left thumbstick pressed")
	, m_Button_LShoulder(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "lshoulder-click", "Left shoulder pressed")
	, m_Button_Up(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "up-click", "Up pressed")
	, m_Button_Down(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "down-click", "Down pressed")
	, m_Button_Left(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "left-click", "Left pressed")
	, m_Button_Right(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "right-click", "Right pressed")
	, m_Button_Enter(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "enter-click", "Select pressed")
	, m_Button_Back(this, XR_ACTION_TYPE_BOOLEAN_INPUT, "back-click", "Back pressed")
	, m_RIndexTrigger(this, XR_ACTION_TYPE_FLOAT_INPUT, "rtrigger", "Right trigger")
	, m_LIndexTrigger(this, XR_ACTION_TYPE_FLOAT_INPUT, "ltrigger", "Left trigger")
	, m_RThumbstick(this, XR_ACTION_TYPE_VECTOR2F_INPUT, "rthumb", "Right thumbstick")
	, m_LThumbstick(this, XR_ACTION_TYPE_VECTOR2F_INPUT, "lthumb", "Left thumbstick")
	, m_RVibration(this, XR_ACTION_TYPE_VIBRATION_OUTPUT, "rvibration", "Right vibration motor")
	, m_LVibration(this, XR_ACTION_TYPE_VIBRATION_OUTPUT, "lvibration", "Left vibration motor")
{
}

InputManager::XboxGamepad::~XboxGamepad()
{
}

void InputManager::XboxGamepad::GetSuggestedBindings(std::vector<ProfileBindings>& outProfiles) const
{
	ProfileBindings result;
	std::vector<XrActionSuggestedBinding>& outBindings = result.Bindings;

#define ADD_BINDING(action, path) outBindings.push_back(XrActionSuggestedBinding{ action, GetXrPath(std::string("/user/gamepad") + path) })

	ADD_BINDING(m_Button_A, "/input/a/click");
	ADD_BINDING(m_Button_B, "/input/b/click");
	ADD_BINDING(m_Button_RThumb, "/input/thumbstick_right/click");
	ADD_BINDING(m_Button_RShoulder, "/input/shoulder_right/click");
	ADD_BINDING(m_Button_X, "/input/x/click");
	ADD_BINDING(m_Button_Y, "/input/y/click");
	ADD_BINDING(m_Button_LThumb, "/input/thumbstick_left/click");
	ADD_BINDING(m_Button_LShoulder, "/input/shoulder_left/click");
	ADD_BINDING(m_Button_Up, "/input/dpad_up/click");
	ADD_BINDING(m_Button_Down, "/input/dpad_down/click");
	ADD_BINDING(m_Button_Left, "/input/dpad_left/click");
	ADD_BINDING(m_Button_Right, "/input/dpad_right/click");
	ADD_BINDING(m_Button_Enter, "/input/menu/click");
	ADD_BINDING(m_Button_Back, "/input/view/click");
	ADD_BINDING(m_RIndexTrigger, "/input/trigger_right/value");
	ADD_BINDING(m_LIndexTrigger, "/input/trigger_left/value");
	ADD_BINDING(m_RThumbstick, "/input/thumbstick_right");
	ADD_BINDING(m_LThumbstick, "/input/thumbstick_left");
	ADD_BINDING(m_RVibration, "/output/haptic_right");
	ADD_BINDING(m_LVibration, "/output/haptic_left");

#undef ADD_BINDING

	result.Profile = GetXrPath("/interaction_profiles/microsoft/xbox_controller");
	outProfiles.push_back(result);
}

void InputManager::XboxGamepad::GetInputState(XrSession session, ovrControllerType controllerType, ovrInputState* inputState)
{
	unsigned int buttons = 0;

	if (m_Button_A.GetDigital(session))
		buttons |= ovrButton_A;

	if (m_Button_B.GetDigital(session))
		buttons |= ovrButton_B;

	if (m_Button_RThumb.GetDigital(session))
		buttons |= ovrButton_RThumb;

	if (m_Button_RShoulder.GetDigital(session))
		buttons |= ovrButton_RShoulder;

	if (m_Button_X.GetDigital(session))
		buttons |= ovrButton_X;

	if (m_Button_Y.GetDigital(session))
		buttons |= ovrButton_Y;

	if (m_Button_LThumb.GetDigital(session))
		buttons |= ovrButton_LThumb;

	if (m_Button_LShoulder.GetDigital(session))
		buttons |= ovrButton_LShoulder;

	if (m_Button_Up.GetDigital(session))
		buttons |= ovrButton_Up;

	if (m_Button_Down.GetDigital(session))
		buttons |= ovrButton_Down;

	if (m_Button_Left.GetDigital(session))
		buttons |= ovrButton_Left;

	if (m_Button_Right.GetDigital(session))
		buttons |= ovrButton_Right;

	if (m_Button_Enter.GetDigital(session))
		buttons |= ovrButton_Enter;

	if (m_Button_Back.GetDigital(session))
		buttons |= ovrButton_Back;

	const Action* triggers[] = { &m_LIndexTrigger, &m_RIndexTrigger };
	const Action* sticks[] = { &m_LThumbstick, &m_RThumbstick };
	float deadzone[] = { 0.24f, 0.265f };
	for (int hand = 0; hand < ovrHand_Count; hand++)
	{
		ovrVector2f thumbstick = sticks[hand]->GetVector(session);
		inputState->IndexTrigger[hand] = triggers[hand]->GetAnalog(session);
		inputState->Thumbstick[hand] = ApplyDeadzone(thumbstick, deadzone[hand]);
		inputState->ThumbstickNoDeadzone[hand] = thumbstick;

		// We don't apply deadzones yet on triggers and grips
		inputState->IndexTriggerNoDeadzone[hand] = inputState->IndexTrigger[hand];
		inputState->HandTriggerNoDeadzone[hand] = inputState->HandTrigger[hand];

		// We have no way to get raw values
		inputState->ThumbstickRaw[hand] = inputState->ThumbstickNoDeadzone[hand];
		inputState->IndexTriggerRaw[hand] = inputState->IndexTriggerNoDeadzone[hand];
		inputState->HandTriggerRaw[hand] = inputState->HandTriggerNoDeadzone[hand];
	}

	inputState->Buttons |= buttons;
}

ovrResult InputManager::XboxGamepad::SetVibration(XrSession session, ovrControllerType controllerType, float frequency, float amplitude)
{
	XrHapticActionInfo info = XR_TYPE(HAPTIC_ACTION_INFO);
	info.action = frequency > 0.5 ? m_RVibration : m_LVibration;
	XrHapticVibration vibration = XR_TYPE(HAPTIC_VIBRATION);
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	vibration.amplitude = amplitude;
	vibration.duration = Runtime::Get().UseHack(Runtime::HACK_MIN_HAPTIC_DURATION) ?
		XR_MIN_HAPTIC_DURATION : MAX_VIBRATION_DURATION;
	CHK_XR(xrApplyHapticFeedback(session, &info, (XrHapticBaseHeader*)&vibration));
	return ovrSuccess;
}

void InputManager::XboxGamepad::GetActiveSets(std::vector<XrActiveActionSet>& outSets) const
{
	outSets.push_back(XrActiveActionSet{ m_ActionSet });
}

ovrResult InputManager::AttachSession(XrSession session)
{
	std::lock_guard<std::mutex> lock(m_ActionMutex);
	m_LastSync = {};

	for (XrSpace space : m_ActionSpaces)
		CHK_XR(xrDestroySpace(space));
	m_ActionSpaces.clear();

	if (session)
	{
		std::vector<XrActionSet> actionSets;
		for (InputDevice* device : m_InputDevices)
		{
			device->GetActionSpaces(session, m_ActionSpaces);
			actionSets.push_back(device->ActionSet());
		}

		XrSessionActionSetsAttachInfo attachInfo = XR_TYPE(SESSION_ACTION_SETS_ATTACH_INFO);
		attachInfo.countActionSets = (uint32_t)actionSets.size();
		attachInfo.actionSets = actionSets.data();
		CHK_XR(xrAttachSessionActionSets(session, &attachInfo));
	}
	return ovrSuccess;
}

ovrResult InputManager::SyncInputState(XrSession session, XrDuration displayPeriod)
{
	std::lock_guard<std::mutex> lock(m_ActionMutex);
	CHK_XR(SyncActions(session));

	for (InputDevice* device : m_InputDevices)
		device->UpdateHaptics(session, displayPeriod);
	return ovrSuccess;
}

XrResult InputManager::SyncActions(XrSession session)
{
	XrActionsSyncInfo syncInfo = XR_TYPE(ACTIONS_SYNC_INFO);
	syncInfo.countActiveActionSets = (uint32_t)m_ActionSets.size();
	syncInfo.activeActionSets = m_ActionSets.data();
	XrResult rs = xrSyncActions(session, &syncInfo);
	if (XR_SUCCEEDED(rs))
		m_LastSync = std::chrono::steady_clock::now();
	return rs;
}

void InputManager::SyncIfStale(XrSession session)
{
	// A failure here (session not running yet) leaves the previous state; the next frame syncs.
	if (std::chrono::steady_clock::now() - m_LastSync > std::chrono::milliseconds(5))
		SyncActions(session);
}
