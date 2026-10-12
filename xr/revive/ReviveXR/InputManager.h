#pragma once

#include "Common.h"
#include "OVR_CAPI.h"
#include "HapticsBuffer.h"

#include <openxr/openxr.h>
#include "../../src/echoxr_pose.h"
#include "../../src/echoxr_settings.h"
#include <chrono>
#include <mutex>
#include <vector>

class Runtime;

class InputManager
{
public:
	struct ProfileBindings
	{
		XrPath Profile;
		std::vector<XrActionSuggestedBinding> Bindings;
	};

	class InputDevice
	{
	public:
		InputDevice(XrInstance instance, const char* actionSetName, const char* localizedName);
		virtual ~InputDevice();

		// Input
		virtual ovrControllerType GetType() const = 0;
		virtual bool IsConnected() const = 0;
		virtual void GetInputState(XrSession session, ovrControllerType controllerType, ovrInputState* inputState) = 0;

		// Bindings
		virtual void GetSuggestedBindings(std::vector<ProfileBindings>& outProfiles) const { }
		virtual void GetActionSpaces(XrSession session, std::vector<XrSpace>& outSpaces) const { }
		virtual void GetActiveSets(std::vector<XrActiveActionSet>& outSets) const { }

		// Haptics
		virtual ovrResult SetVibration(XrSession session, ovrControllerType controllerType, float frequency, float amplitude) { return ovrSuccess; }
		virtual void SubmitVibration(ovrControllerType controllerType, const ovrHapticsBuffer* buffer) { }
		virtual void GetVibrationState(ovrHandType hand, ovrHapticsPlaybackState* outState) { }
		virtual void UpdateHaptics(XrSession session, XrDuration displayPeriod) { }

		XrActionSet ActionSet() const { return m_ActionSet; }

	protected:
		static ovrVector2f ApplyDeadzone(ovrVector2f axis, float deadZone);
		static ovrButton TrackpadToDPad(ovrVector2f trackpad);

		XrActionSet m_ActionSet;
	};

	class Action
	{
	public:
		Action() : m_Action(XR_NULL_HANDLE) {}
		Action(InputDevice* device, XrActionType type, const char* actionName, const char* localizedName, bool handedAction = false);

		bool GetDigital(XrSession session, ovrHandType hand = ovrHand_Left) const;
		bool IsPressed(XrSession session, ovrHandType hand = ovrHand_Left) const;
		bool IsReleased(XrSession session, ovrHandType hand = ovrHand_Left) const;
		float GetAnalog(XrSession session, ovrHandType hand = ovrHand_Left) const;
		ovrVector2f GetVector(XrSession session, ovrHandType hand = ovrHand_Left) const;
		XrSpace CreateSpace(XrSession session, ovrHandType hand = ovrHand_Left, const XrPosef& poseInActionSpace = { { 0, 0, 0, 1 }, { 0, 0, 0 } }) const;

		operator XrAction() const { return m_Action; }

	private:
		bool m_IsHanded;
		XrAction m_Action;
	};

	class OculusTouch : public InputDevice
	{
	public:
		OculusTouch(XrInstance instance);
		virtual ~OculusTouch();

		virtual ovrControllerType GetType() const override;
		virtual bool IsConnected() const override;
		virtual void GetInputState(XrSession session, ovrControllerType controllerType, ovrInputState* inputState) override;
		virtual void GetSuggestedBindings(std::vector<ProfileBindings>& outProfiles) const override;
		virtual void GetActionSpaces(XrSession session, std::vector<XrSpace>& outSpaces) const override;
		virtual void GetActiveSets(std::vector<XrActiveActionSet>& outSets) const override;

		virtual ovrResult SetVibration(XrSession session, ovrControllerType controllerType, float frequency, float amplitude) override;
		virtual void SubmitVibration(ovrControllerType controllerType, const ovrHapticsBuffer* buffer) override;
		virtual void GetVibrationState(ovrHandType hand, ovrHapticsPlaybackState* outState) override { *outState = m_HapticsBuffer[hand].GetState(); }
		virtual void UpdateHaptics(XrSession session, XrDuration displayPeriod) override;

	private:
		enum Layout { Layout_Touch, Layout_Index, Layout_WMR };
		ProfileBindings GetLayoutBindings(Layout layout) const;

		Action m_Button_AX;
		Action m_Button_BY;
		Action m_Button_Thumb;
		Action m_Button_Enter;
		Action m_Button_Home;

		Action m_Touch_AX;
		Action m_Touch_BY;
		Action m_Touch_Thumb;
		Action m_Touch_ThumbRest;
		Action m_Touch_IndexTrigger;

		Action m_IndexTrigger;
		Action m_HandTrigger;
		Action m_Thumbstick;

		// For WMR profile hack
		Action m_Trackpad_Buttons;

		// EchoXR: both poses stay bound, EchoXR.txt's HandPose picks one when locating.
		Action m_GripPose;
		Action m_AimPose;
		Action m_Vibration;
		HapticsBuffer m_HapticsBuffer[ovrHand_Count];
	};

	class OculusRemote : public InputDevice
	{
	public:
		OculusRemote(XrInstance instance);
		virtual ~OculusRemote() { }

		virtual ovrControllerType GetType() const override { return ovrControllerType_Remote; }
		virtual bool IsConnected() const override;
		virtual void GetInputState(XrSession session, ovrControllerType controllerType, ovrInputState* inputState) override;
		virtual void GetActiveSets(std::vector<XrActiveActionSet>& outSets) const override;

	private:
		bool m_IsConnected;

		Action m_Toggle_Connected;
		Action m_Button_Up;
		Action m_Button_Down;
		Action m_Button_Left;
		Action m_Button_Right;
		Action m_Button_Enter;
		Action m_Button_Back;
		Action m_Button_VolUp;
		Action m_Button_VolDown;
	};

	class XboxGamepad : public InputDevice
	{
	public:
		XboxGamepad(XrInstance instance);
		virtual ~XboxGamepad();

		virtual ovrControllerType GetType() const override { return ovrControllerType_XBox; }
		virtual bool IsConnected() const override { return true; }
		virtual void GetInputState(XrSession session, ovrControllerType controllerType, ovrInputState* inputState) override;
		virtual void GetSuggestedBindings(std::vector<ProfileBindings>& outProfiles) const override;
		virtual void GetActiveSets(std::vector<XrActiveActionSet>& outSets) const override;
		virtual ovrResult SetVibration(XrSession session, ovrControllerType controllerType, float frequency, float amplitude) override;

	private:
		Action m_Button_A;
		Action m_Button_B;
		Action m_Button_RThumb;
		Action m_Button_RShoulder;
		Action m_Button_X;
		Action m_Button_Y;
		Action m_Button_LThumb;
		Action m_Button_LShoulder;
		Action m_Button_Up;
		Action m_Button_Down;
		Action m_Button_Left;
		Action m_Button_Right;
		Action m_Button_Enter;
		Action m_Button_Back;
		Action m_RIndexTrigger;
		Action m_LIndexTrigger;
		Action m_RThumbstick;
		Action m_LThumbstick;
		Action m_RVibration;
		Action m_LVibration;
	};

	InputManager(XrInstance instance);
	~InputManager();

	ovrResult AttachSession(XrSession session);
	ovrResult SyncInputState(XrSession session, XrDuration displayPeriod);

	ovrResult SetControllerVibration(ovrSession session, ovrControllerType controllerType, float frequency, float amplitude);
	ovrResult GetInputState(ovrSession session, ovrControllerType controllerType, ovrInputState* inputState);
	ovrResult SubmitControllerVibration(ovrSession session, ovrControllerType controllerType, const ovrHapticsBuffer* buffer);
	ovrResult GetControllerVibrationState(ovrSession session, ovrControllerType controllerType, ovrHapticsPlaybackState* outState);

	void GetTrackingState(ovrSession session, ovrTrackingState* outState, double absTime);
	static void LogInteractionProfiles(XrInstance instance, XrSession session);
	static bool PoseLogVariable();
	ovrResult GetDevicePoses(ovrSession session, ovrTrackedDeviceType* deviceTypes, int deviceCount, double absTime, ovrPoseStatef* outDevicePoses);

protected:
	static XrPath s_SubActionPaths[ovrHand_Count];

	Action m_ControllerPose;
	std::vector<InputDevice*> m_InputDevices;
	std::vector<XrSpace> m_ActionSpaces;
	std::vector<XrActiveActionSet> m_ActionSets;

	ovrTrackingState m_LastTrackingState;

	// EchoXR: Echo reads input and tracking from several threads, and not every runtime
	// copes with overlapping action calls, so all of them take this lock. Reads also sync
	// the actions when the last sync is older than a few milliseconds: before the first
	// frame nothing synced them, and the controllers looked untracked.
	std::mutex m_ActionMutex;
	std::chrono::steady_clock::time_point m_LastSync;
	XrResult SyncActions(XrSession session);
	void SyncIfStale(XrSession session);

	// EchoXR: action spaces 0-1 are the grip poses (left, right), 2-3 the aim poses.
	static constexpr size_t kAimSpaces = 2;

	// EchoXR: a pose with its velocities in the session's tracking space, and what the runtime
	// said about it.
	struct Located
	{
		echoxr::PoseMotion motion = {};
		XrSpaceLocationFlags locationFlags = 0;
		XrSpaceVelocityFlags velocityFlags = 0;
		bool fallback = false;     // located at the frame's predicted time, not the one asked for
	};
	static Located LocateInTracking(ovrSession session, XrSpace space, XrTime time);
	bool LocateHand(ovrSession session, int hand, XrTime time, const echoxr::ControllerSettings& settings,
		Located& out, echoxr::PoseMotion* beforeOffset = nullptr) const;

	// EchoXR: PoseLog = 1 in EchoXR.txt (or ECHOXR_POSE_LOG=1) logs, once a second, the aim
	// pose and the hand pose Echo gets, both relative to the grip pose.
	// Per hand, between two log lines: how many poses had valid velocities, how far ahead of
	// now the game asked, how often the locate fell back to the frame's time, and the reported
	// velocities against ones differentiated from consecutive poses (Echo throws and pushes off
	// with them). An angular velocity the runtime gives in the controller's own frame instead of
	// the tracking space's shows up as a small "local" error and a large "world" one.
	struct PoseLogStats
	{
		int samples = 0, velocityValid = 0, fallbacks = 0, leadCount = 0, angCount = 0, linCount = 0;
		double leadSum = 0.0, leadMin = 1e9, leadMax = -1e9;
		double angRatio = 0.0, angErrWorld = 0.0, angErrLocal = 0.0, linRatio = 0.0, linErr = 0.0;
		bool hasPrev = false;
		ovrPoseStatef prev = {};
	};
	PoseLogStats m_PoseLogStats[ovrHand_Count];
	std::chrono::steady_clock::time_point m_LastPoseLog;
	void RecordPose(int hand, XrSpaceVelocityFlags velocityFlags, const ovrPoseStatef& pose, double lead, bool fallback);
	void LogPoses(XrTime time, const echoxr::ControllerSettings& settings);

	static unsigned int SpaceRelationToPoseState(const Located& location, double time, ovrPoseStatef& lastPoseState, ovrPoseStatef& outPoseState);
};

