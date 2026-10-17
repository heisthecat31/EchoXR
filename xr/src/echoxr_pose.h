// Controller pose math EchoXR's runtime uses, kept free of Windows and OpenXR calls so
// tests/runtime_units.cpp can check it on any machine. OpenXR axes: +X right, +Y up, -Z forward.
#pragma once

#include <cmath>

namespace echoxr {

struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };
struct Pose { Quat orientation; Vec3 position; };

inline Quat Multiply(Quat a, Quat b)
{
	return {
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

inline Vec3 Rotate(Quat q, Vec3 v)
{
	const Quat p = Multiply(Multiply(q, Quat{ v.x, v.y, v.z, 0.0f }), Quat{ -q.x, -q.y, -q.z, q.w });
	return { p.x, p.y, p.z };
}

// b expressed in a's frame applied to a: the pose of b in the space a is given in.
inline Pose Compose(const Pose& a, const Pose& b)
{
	const Vec3 r = Rotate(a.orientation, b.position);
	return { Multiply(a.orientation, b.orientation),
		{ a.position.x + r.x, a.position.y + r.y, a.position.z + r.z } };
}

inline Quat RotationX(float degrees)
{
	const float half = degrees * 3.14159265f / 360.0f;
	return { sinf(half), 0.0f, 0.0f, cosf(half) };
}

// A rotation as an angle in degrees (0..180) about a unit axis, for the pose log.
inline void ToAxisAngle(Quat q, float& degrees, Vec3& axis)
{
	if (q.w < 0.0f)
		q = { -q.x, -q.y, -q.z, -q.w };
	const float s = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z);
	degrees = 2.0f * atan2f(s, q.w) * 180.0f / 3.14159265f;
	axis = s > 1e-6f ? Vec3{ q.x / s, q.y / s, q.z / s } : Vec3{ 1.0f, 0.0f, 0.0f };
}

inline Quat Conjugate(Quat q) { return { -q.x, -q.y, -q.z, q.w }; }

inline Vec3 Add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }

inline Vec3 Cross(Vec3 a, Vec3 b)
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline Pose Inverse(const Pose& p)
{
	const Quat q = Conjugate(p.orientation);
	const Vec3 t = Rotate(q, p.position);
	return { q, { -t.x, -t.y, -t.z } };
}

inline Quat RotationY(float degrees)
{
	const float half = degrees * 3.14159265f / 360.0f;
	return { 0.0f, sinf(half), 0.0f, cosf(half) };
}

inline Quat RotationZ(float degrees)
{
	const float half = degrees * 3.14159265f / 360.0f;
	return { 0.0f, 0.0f, sinf(half), cosf(half) };
}

// A pose with its velocities, all in one space.
struct PoseMotion
{
	Pose pose;
	Vec3 linear;    // m/s, of the pose's origin
	Vec3 angular;   // rad/s
};

// A motion located in the reference space `origin` re-expressed in a space whose pose in that
// reference space is `space`: the pose and both velocities (velocities are rotated, the
// spaces don't move against each other).
inline PoseMotion ToSpace(const Pose& space, const PoseMotion& m)
{
	const Pose inv = Inverse(space);
	return { Compose(inv, m.pose), Rotate(inv.orientation, m.linear), Rotate(inv.orientation, m.angular) };
}

// The motion of a point fixed to the moving pose at `offset` (in the pose's own frame). With
// `lever`, its linear velocity gains the turn's contribution (w x r), as a point on a rigid
// controller moves; without it, the velocity stays the one of the pose's origin.
inline PoseMotion Attach(const PoseMotion& m, const Pose& offset, bool lever)
{
	PoseMotion out = { Compose(m.pose, offset), m.linear, m.angular };
	if (lever)
		out.linear = Add(m.linear, Cross(m.angular, Rotate(m.pose.orientation, offset.position)));
	return out;
}

// --- Oculus Touch: OpenXR grip pose -> LibOVR hand pose ------------------------------------
//
// LibOVR reports a Touch controller at its own origin, which OpenXR doesn't define. Meta's
// OVRPlugin converts between them (legacy -> grip: +60° about X and (0, -0.03, +0.04) m) and
// SteamVR's Oculus controller models agree to the millimetre: the LibOVR pose has the aim
// pose's orientation, 5.5 cm behind it. Expressed from the grip pose, which every runtime
// defines the same way (the aim pose has changed on Quest), that's the transform below, the
// same for both hands.
constexpr Pose kGripToOvrHand = {
	{ -0.5f, 0.0f, 0.0f, 0.8660254f },        // -60° about X
	{ 0.0f, -0.019641f, -0.045981f } };

// --- The player's own turn and offset of the hand (EchoXR.txt) ------------------------------
//
// Given for the right hand: pitch tips the hand's forward up, yaw turns it inwards (to the
// left), roll tilts its top outwards (to the right); the offset moves it inwards, up and
// forwards, in centimetres. The left hand gets the mirror image (x negated).
inline Pose UserHandOffset(float pitch, float yaw, float roll, float inwardsCm, float upCm, float forwardsCm, int hand)
{
	const Quat q = Multiply(Multiply(RotationY(yaw), RotationX(pitch)), RotationZ(-roll));
	Pose right = { q, { -inwardsCm / 100.0f, upCm / 100.0f, -forwardsCm / 100.0f } };
	if (hand == 0)  // left: mirrored across the x axis
		return { { q.x, -q.y, -q.z, q.w }, { -right.position.x, right.position.y, right.position.z } };
	return right;
}

} // namespace echoxr
