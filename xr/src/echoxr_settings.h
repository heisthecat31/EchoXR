// EchoXR's controller settings (EchoXR.txt) and the small decisions made from them, kept free
// of Windows and OpenXR calls so tests can check them on any machine.
//
// EchoXR.txt is `Key = Value` lines, `#` or `;` comments, the last value of a key wins. Keys
// are case-insensitive; an unknown key, a value out of range or one that isn't a number is
// noted and the key keeps its default (or is clamped into range).
//
// Nothing here changes how fast a hand moves: the settings turn and move the hand Echo sees,
// they don't scale its speed or predict further ahead. Throws and pushes stay as strong as the
// headset measures them.
#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace echoxr {

// Which OpenXR pose becomes the hand Echo sees.
enum class HandPose
{
	Grip,   // the grip pose moved to LibOVR's controller origin (kGripToOvrHand): a Rift's hand
	Aim,    // the aim pose as the runtime gives it, as Revive did
};

// A button combination standing in for the Menu button, for runtimes that keep it to themselves.
enum class MenuChord
{
	Off,
	XY,     // left X and Y together
};

struct ControllerSettings
{
	HandPose handPose = HandPose::Grip;
	// The hand's turn, in degrees, as the right hand sees it (the left hand is mirrored):
	// pitch up, yaw inwards, roll outwards.
	float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
	// The hand's offset, in centimetres, right hand: inwards, up, forwards.
	float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
	float thumbstickDeadzone = 0.24f;
	float triggerDeadzone = 0.0f;
	float gripDeadzone = 0.0f;
	float vibration = 100.0f;   // percent
	MenuChord menuChord = MenuChord::Off;
	bool recenterOnStart = true;
	bool poseLog = false;
};

// Every key EchoXR.txt knows, in the order the launcher's Controllers plugin shows them.
constexpr const char* kControllerKeys[] = {
	"HandPose", "Pitch", "Yaw", "Roll", "OffsetX", "OffsetY", "OffsetZ",
	"ThumbstickDeadzone", "TriggerDeadzone", "GripDeadzone", "Vibration",
	"MenuChord", "RecenterOnStart", "PoseLog" };

constexpr float kMaxAngle = 45.0f;      // degrees
constexpr float kMaxOffset = 5.0f;      // centimetres

namespace detail {

inline std::string Trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
		a++;
	while (b > a && std::isspace((unsigned char)s[b - 1]))
		b--;
	return s.substr(a, b - a);
}

inline std::string Lower(std::string s)
{
	for (char& c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

inline bool ParseNumber(const std::string& text, float& out)
{
	if (text.empty())
		return false;
	char* end = nullptr;
	const double v = std::strtod(text.c_str(), &end);
	if (end != text.c_str() + text.size() || !std::isfinite(v))
		return false;
	out = (float)v;
	return true;
}

inline bool ParseBool(const std::string& text, bool& out)
{
	const std::string v = Lower(text);
	if (v == "1" || v == "true" || v == "on" || v == "yes")
		out = true;
	else if (v == "0" || v == "false" || v == "off" || v == "no")
		out = false;
	else
		return false;
	return true;
}

} // namespace detail

// Reads EchoXR.txt's text. Notes (when given) get a line for each key that was ignored or
// changed, for runtime.log.
inline ControllerSettings ParseControllerSettings(const std::string& text, std::vector<std::string>* notes = nullptr)
{
	using namespace detail;
	ControllerSettings s;
	auto note = [notes](const std::string& line) { if (notes) notes->push_back(line); };

	auto number = [&](const std::string& key, const std::string& value, float& field, float lo, float hi)
	{
		float v;
		if (!ParseNumber(value, v))
		{
			note(key + ": \"" + value + "\" isn't a number, kept " + std::to_string(field));
			return;
		}
		if (v < lo || v > hi)
		{
			const float clamped = v < lo ? lo : hi;
			note(key + ": " + value + " is out of range, used " + std::to_string(clamped));
			v = clamped;
		}
		field = v;
	};
	auto flag = [&](const std::string& key, const std::string& value, bool& field)
	{
		if (!ParseBool(value, field))
			note(key + ": \"" + value + "\" isn't 1 or 0, kept " + (field ? "1" : "0"));
	};

	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		std::string line = Trim(text.substr(pos, end - pos));
		pos = end + 1;

		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			note("ignored a line without '=': " + line);
			continue;
		}
		const std::string key = Trim(line.substr(0, eq));
		const std::string value = Trim(line.substr(eq + 1));
		const std::string k = Lower(key);

		if (k == "handpose")
		{
			const std::string v = Lower(value);
			if (v == "grip")
				s.handPose = HandPose::Grip;
			else if (v == "aim")
				s.handPose = HandPose::Aim;
			else
				note(key + ": \"" + value + "\" isn't grip or aim");
		}
		else if (k == "pitch") number(key, value, s.pitch, -kMaxAngle, kMaxAngle);
		else if (k == "yaw") number(key, value, s.yaw, -kMaxAngle, kMaxAngle);
		else if (k == "roll") number(key, value, s.roll, -kMaxAngle, kMaxAngle);
		else if (k == "offsetx") number(key, value, s.offsetX, -kMaxOffset, kMaxOffset);
		else if (k == "offsety") number(key, value, s.offsetY, -kMaxOffset, kMaxOffset);
		else if (k == "offsetz") number(key, value, s.offsetZ, -kMaxOffset, kMaxOffset);
		else if (k == "thumbstickdeadzone") number(key, value, s.thumbstickDeadzone, 0.0f, 0.5f);
		else if (k == "triggerdeadzone") number(key, value, s.triggerDeadzone, 0.0f, 0.3f);
		else if (k == "gripdeadzone") number(key, value, s.gripDeadzone, 0.0f, 0.3f);
		else if (k == "vibration") number(key, value, s.vibration, 0.0f, 100.0f);
		else if (k == "menuchord")
		{
			const std::string v = Lower(value);
			if (v == "off" || v == "0")
				s.menuChord = MenuChord::Off;
			else if (v == "xy" || v == "x+y")
				s.menuChord = MenuChord::XY;
			else
				note(key + ": \"" + value + "\" isn't off or xy");
		}
		else if (k == "recenteronstart") flag(key, value, s.recenterOnStart);
		else if (k == "poselog") flag(key, value, s.poseLog);
		else
			note("unknown key " + key);
	}
	return s;
}

// An analog trigger or grip value with a deadzone: 0 up to it, then the rest stretched to 0..1.
inline float ApplyTriggerDeadzone(float value, float deadzone)
{
	if (deadzone <= 0.0f)
		return value;
	if (value <= deadzone)
		return 0.0f;
	const float v = (value - deadzone) / (1.0f - deadzone);
	return v > 1.0f ? 1.0f : v;
}

// A vibration amplitude (0..1) at the player's strength (percent).
inline float ScaleVibration(float amplitude, float percent)
{
	const float v = amplitude * percent / 100.0f;
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// LibOVR's button bits used here (OVR_CAPI.h's ovrButton_X, _Y and _Enter).
constexpr unsigned kButtonX = 0x00000100, kButtonY = 0x00000200, kButtonEnter = 0x00100000;

// The Menu button from the chord: with left X and Y both held, Menu is pressed instead of them.
inline unsigned ApplyMenuChord(unsigned buttons, MenuChord chord)
{
	if (chord == MenuChord::XY && (buttons & (kButtonX | kButtonY)) == (kButtonX | kButtonY))
		return (buttons & ~(kButtonX | kButtonY)) | kButtonEnter;
	return buttons;
}

} // namespace echoxr
