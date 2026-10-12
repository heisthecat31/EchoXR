// Unit tests for the controller settings (xr/src/echoxr_settings.h) and the pose math
// (xr/src/echoxr_pose.h). Only those two headers: it builds and runs on any desktop too,
//   c++ -std=c++17 -Ixr/src tests/controller_units.cpp -o controller_units && ./controller_units
#include "echoxr_pose.h"
#include "echoxr_settings.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) \
	do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

using namespace echoxr;

static bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

static bool NearVec(Vec3 a, Vec3 b, float tolerance)
{
	return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
}

// Same rotation (q and -q are the same one) within about a tenth of a degree.
static bool SameRotation(Quat a, Quat b)
{
	return std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) > 0.9999996f;
}

static bool HasNote(const std::vector<std::string>& notes, const std::string& part)
{
	for (const std::string& n : notes)
		if (n.find(part) != std::string::npos)
			return true;
	return false;
}

static void Parsing()
{
	std::vector<std::string> notes;
	ControllerSettings d = ParseControllerSettings("", &notes);
	CHECK(notes.empty());
	CHECK(d.handPose == HandPose::Grip);
	CHECK(d.pitch == 0.0f && d.yaw == 0.0f && d.roll == 0.0f);
	CHECK(d.offsetX == 0.0f && d.offsetY == 0.0f && d.offsetZ == 0.0f);
	CHECK(d.thumbstickDeadzone == 0.24f && d.triggerDeadzone == 0.0f && d.gripDeadzone == 0.0f);
	CHECK(d.vibration == 100.0f && d.menuChord == MenuChord::Off && d.recenterOnStart && !d.poseLog);

	notes.clear();
	ControllerSettings s = ParseControllerSettings(
		"# Kept by the launcher\r\n"
		"; a comment\r\n"
		"  handpose = AIM \r\n"
		"Pitch = -12.5\r\n"
		"YAW=3\n"
		"Roll = 90\n"            // clamped to 45
		"OffsetZ = -7\n"         // clamped to -5
		"OffsetY = two\n"        // not a number: stays 0
		"ThumbstickDeadzone = 0.1\n"
		"TriggerDeadzone = 0.05\n"
		"GripDeadzone = 0.2\n"
		"Vibration = 40\n"
		"MenuChord = x+y\n"
		"RecenterOnStart = off\n"
		"PoseLog = 1\n"
		"Pitch = -10\n"          // the last value wins
		"Speed = 2\n"            // unknown
		"no equals sign\n", &notes);
	CHECK(s.handPose == HandPose::Aim);
	CHECK(s.pitch == -10.0f && s.yaw == 3.0f && s.roll == kMaxAngle);
	CHECK(s.offsetZ == -kMaxOffset && s.offsetY == 0.0f);
	CHECK(Near(s.thumbstickDeadzone, 0.1f, 1e-6f) && Near(s.triggerDeadzone, 0.05f, 1e-6f) && Near(s.gripDeadzone, 0.2f, 1e-6f));
	CHECK(s.vibration == 40.0f && s.menuChord == MenuChord::XY && !s.recenterOnStart && s.poseLog);
	CHECK(HasNote(notes, "Roll"));
	CHECK(HasNote(notes, "OffsetZ"));
	CHECK(HasNote(notes, "OffsetY"));
	CHECK(HasNote(notes, "unknown key Speed"));
	CHECK(HasNote(notes, "without '='"));
	CHECK(notes.size() == 5);

	// Bad words keep the defaults.
	notes.clear();
	ControllerSettings bad = ParseControllerSettings("HandPose = palm\nMenuChord = a+b\nPoseLog = maybe\nVibration = 1e9\nPitch = nan\n", &notes);
	CHECK(bad.handPose == HandPose::Grip && bad.menuChord == MenuChord::Off && !bad.poseLog);
	CHECK(bad.vibration == 100.0f && bad.pitch == 0.0f);
	CHECK(notes.size() == 5);

	// Every key the launcher may write is known.
	for (const char* key : kControllerKeys)
	{
		notes.clear();
		ParseControllerSettings(std::string(key) + " = 0\n", &notes);
		CHECK(!HasNote(notes, "unknown key"));
	}
}

static void GripToLibOvr()
{
	const Quat q = kGripToOvrHand.orientation;
	CHECK(Near(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f, 1e-6f));
	float degrees;
	Vec3 axis;
	ToAxisAngle(q, degrees, axis);
	CHECK(Near(degrees, 60.0f, 0.01f));
	CHECK(NearVec(axis, Vec3{ -1.0f, 0.0f, 0.0f }, 1e-5f));

	// SteamVR's Oculus Touch models (oculus_quest2/quest_plus/cv1 _controller_left.json agree):
	// LibOVR's pose is the aim pose moved 5.5 cm back along it; grip then kGripToOvrHand lands there.
	const Pose grip = { RotationX(20.6f), { 0.007f, -0.00183f, 0.10195f } };
	const Pose aim = { RotationX(-39.4f), { 0.007f, -0.03895f, 0.0095f } };
	const Pose expected = Compose(aim, Pose{ { 0, 0, 0, 1 }, { 0.0f, 0.0f, 0.055f } });
	const Pose hand = Compose(grip, kGripToOvrHand);
	CHECK(SameRotation(hand.orientation, expected.orientation));
	CHECK(NearVec(hand.position, expected.position, 0.001f));

	// Meta's own conversion (OVRPlugin: grip = LibOVR pose, then +60° about X and (0, -0.03, +0.04) m) undone.
	const Pose ovr = { RotationX(-12.0f), { 0.1f, 1.2f, -0.3f } };
	const Pose back = Compose(Compose(ovr, Pose{ RotationX(60.0f), { 0.0f, -0.03f, 0.04f } }), kGripToOvrHand);
	CHECK(SameRotation(back.orientation, ovr.orientation));
	CHECK(NearVec(back.position, ovr.position, 0.0005f));
}

static void Mirroring()
{
	const Vec3 forward = { 0, 0, -1 }, up = { 0, 1, 0 };
	for (int hand = 0; hand < 2; hand++)
	{
		const float inwards = hand == 1 ? -1.0f : 1.0f;     // inwards is -x for the right hand
		CHECK(Rotate(UserHandOffset(10, 0, 0, 0, 0, 0, hand).orientation, forward).y > 0.1f);              // pitch: up
		CHECK(Rotate(UserHandOffset(0, 10, 0, 0, 0, 0, hand).orientation, forward).x * inwards > 0.1f);    // yaw: inwards
		CHECK(Rotate(UserHandOffset(0, 0, 10, 0, 0, 0, hand).orientation, up).x * inwards < -0.1f);        // roll: top outwards
		const Pose moved = UserHandOffset(0, 0, 0, 2, 3, 4, hand);
		CHECK(Near(moved.position.x, 0.02f * inwards, 1e-6f));
		CHECK(Near(moved.position.y, 0.03f, 1e-6f));
		CHECK(Near(moved.position.z, -0.04f, 1e-6f));
		CHECK(SameRotation(UserHandOffset(0, 0, 0, 0, 0, 0, hand).orientation, Quat{ 0, 0, 0, 1 }));
	}
	// The hands are mirror images: the same settings turn them by the same angle.
	float a, b;
	Vec3 axis;
	ToAxisAngle(UserHandOffset(20, 15, 10, 0, 0, 0, 0).orientation, a, axis);
	ToAxisAngle(UserHandOffset(20, 15, 10, 0, 0, 0, 1).orientation, b, axis);
	CHECK(Near(a, b, 1e-3f));
}

// A rigid motion of `m` over `dt` seconds (angular velocity in the space's frame).
static Pose Advance(const PoseMotion& m, float dt)
{
	const float w = std::sqrt(m.angular.x * m.angular.x + m.angular.y * m.angular.y + m.angular.z * m.angular.z);
	Quat turn = { 0, 0, 0, 1 };
	if (w > 0.0f)
	{
		const float s = std::sin(w * dt / 2) / w;
		turn = { m.angular.x * s, m.angular.y * s, m.angular.z * s, std::cos(w * dt / 2) };
	}
	return { Multiply(turn, m.pose.orientation), Add(m.pose.position, Vec3{ m.linear.x * dt, m.linear.y * dt, m.linear.z * dt }) };
}

static Vec3 Difference(const Pose& a, const Pose& b, float dt)
{
	return { (b.position.x - a.position.x) / dt, (b.position.y - a.position.y) / dt, (b.position.z - a.position.z) / dt };
}

static const PoseMotion kSwing = {
	{ { 0.1f, 0.3f, -0.2f, 0.9273618f }, { 0.25f, 1.1f, -0.4f } },
	{ 1.5f, -0.4f, 2.0f },
	{ 4.0f, -6.0f, 2.5f } };

static void Velocities()
{
	const float dt = 1e-4f;

	// Grip -> LibOVR's point: its velocity includes the turn, as a point on the controller moves.
	const PoseMotion hand = Attach(kSwing, kGripToOvrHand, true);
	const Vec3 measured = Difference(Compose(kSwing.pose, kGripToOvrHand), Compose(Advance(kSwing, dt), kGripToOvrHand), dt);
	CHECK(NearVec(hand.linear, measured, 0.01f));
	CHECK(NearVec(hand.angular, kSwing.angular, 0.0f));

	// Into the recentered tracking space: the pose and the velocities turn with it. This is what
	// Virtual Desktop's runtime leaves out when the base space has a pose of its own.
	const Pose tracking = { RotationY(90.0f), { 0.5f, 0.0f, -1.0f } };
	const PoseMotion inTracking = ToSpace(tracking, hand);
	const Pose t0 = Compose(Inverse(tracking), Compose(kSwing.pose, kGripToOvrHand));
	const Pose t1 = Compose(Inverse(tracking), Compose(Advance(kSwing, dt), kGripToOvrHand));
	CHECK(SameRotation(inTracking.pose.orientation, t0.orientation));
	CHECK(NearVec(inTracking.pose.position, t0.position, 1e-5f));
	CHECK(NearVec(inTracking.linear, Difference(t0, t1, dt), 0.01f));
	CHECK(NearVec(inTracking.angular, Rotate(Conjugate(tracking.orientation), hand.angular), 1e-5f));
	// Unturned, a sideways throw would have gone off by the recenter's 90°.
	CHECK(!NearVec(hand.linear, inTracking.linear, 0.5f));
}

// Fair play: no setting changes how fast the hand moves, only where it is and how it's turned.
static void FairPlay()
{
	const PoseMotion hand = Attach(kSwing, kGripToOvrHand, true);
	const float angles[] = { -kMaxAngle, -10.0f, 0.0f, 25.0f, kMaxAngle };
	const float offsets[] = { -kMaxOffset, 0.0f, kMaxOffset };
	for (int side = 0; side < 2; side++)
		for (float pitch : angles)
			for (float yaw : angles)
				for (float roll : angles)
					for (float off : offsets)
					{
						const PoseMotion seen = Attach(hand, UserHandOffset(pitch, yaw, roll, off, -off, off, side), false);
						CHECK(NearVec(seen.linear, hand.linear, 0.0f));
						CHECK(NearVec(seen.angular, hand.angular, 0.0f));
					}
	// No key in EchoXR.txt is about speed or prediction.
	for (const char* key : kControllerKeys)
	{
		std::string k = key;
		for (char& c : k)
			c = (char)std::tolower((unsigned char)c);
		for (const char* word : { "velocity", "speed", "strength", "multiplier", "predict", "boost", "scale" })
			CHECK(k.find(word) == std::string::npos);
	}
}

static void InputHelpers()
{
	CHECK(ApplyTriggerDeadzone(0.5f, 0.0f) == 0.5f);
	CHECK(ApplyTriggerDeadzone(0.1f, 0.2f) == 0.0f);
	CHECK(ApplyTriggerDeadzone(0.2f, 0.2f) == 0.0f);
	CHECK(Near(ApplyTriggerDeadzone(0.6f, 0.2f), 0.5f, 1e-6f));
	CHECK(ApplyTriggerDeadzone(1.0f, 0.3f) == 1.0f);

	CHECK(ScaleVibration(0.8f, 100.0f) == 0.8f);
	CHECK(Near(ScaleVibration(0.8f, 50.0f), 0.4f, 1e-6f));
	CHECK(ScaleVibration(0.8f, 0.0f) == 0.0f);

	const unsigned xy = kButtonX | kButtonY;
	CHECK(ApplyMenuChord(xy | 0x1, MenuChord::Off) == (xy | 0x1));
	CHECK(ApplyMenuChord(xy | 0x1, MenuChord::XY) == (kButtonEnter | 0x1));
	CHECK(ApplyMenuChord(kButtonX, MenuChord::XY) == kButtonX);
}

int main()
{
	Parsing();
	GripToLibOvr();
	Mirroring();
	Velocities();
	FairPlay();
	InputHelpers();
	if (g_failures)
	{
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
