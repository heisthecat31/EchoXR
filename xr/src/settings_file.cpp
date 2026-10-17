// EchoXR.txt, the controller settings, read from beside this DLL (ECHOXR_SETTINGS names another
// file). The file is checked for changes at most twice a second, on the calls that need it, so
// a change made while Echo runs applies within half a second. No thread of its own: the game's
// LibOVR loader may unload the runtime, and a thread left running would then crash.
#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

#include "echoxr_settings.h"

void EchoXR_Log(const char* fmt, ...);

namespace {

std::mutex g_lock;
echoxr::ControllerSettings g_settings;
std::wstring g_path;
bool g_checked = false;
ULONGLONG g_lastCheck = 0;
bool g_present = false;
FILETIME g_written = {};
ULONGLONG g_size = 0;

std::string Utf8(const std::wstring& w)
{
	if (w.empty())
		return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

std::wstring SettingsPath()
{
	wchar_t buf[32768];
	DWORD n = GetEnvironmentVariableW(L"ECHOXR_SETTINGS", buf, 32768);
	if (n > 0 && n < 32768)
		return std::wstring(buf, n);

	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)&SettingsPath, &self);
	n = GetModuleFileNameW(self, buf, 32768);
	std::wstring path(buf, n < 32768 ? n : 0);
	const size_t slash = path.find_last_of(L"\\/");
	path = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
	return path + L"EchoXR.txt";
}

// The file's text, or false when it can't be read just now (the launcher replacing it).
bool ReadText(const std::wstring& path, std::string& out)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	out.clear();
	char buf[4096];
	DWORD got = 0;
	bool ok = true;
	while (out.size() < 65536)
	{
		if (!ReadFile(h, buf, sizeof(buf), &got, nullptr))
		{
			ok = false;
			break;
		}
		if (got == 0)
			break;
		out.append(buf, got);
	}
	CloseHandle(h);
	return ok;
}

const char* Name(echoxr::HandPose p) { return p == echoxr::HandPose::Aim ? "aim" : "grip"; }
const char* Name(echoxr::MenuChord c) { return c == echoxr::MenuChord::XY ? "X+Y" : "off"; }

void Describe(const echoxr::ControllerSettings& s, const char* source)
{
	EchoXR_Log("Controller settings (%s): hand pose %s, turn pitch %.1f yaw %.1f roll %.1f deg, offset %.1f %.1f %.1f cm, "
		"deadzones stick %.2f trigger %.2f grip %.2f, vibration %.0f%%, menu chord %s, recenter on start %d, pose log %d",
		source, Name(s.handPose), s.pitch, s.yaw, s.roll, s.offsetX, s.offsetY, s.offsetZ,
		s.thumbstickDeadzone, s.triggerDeadzone, s.gripDeadzone, s.vibration, Name(s.menuChord),
		s.recenterOnStart ? 1 : 0, s.poseLog ? 1 : 0);
}

} // namespace

echoxr::ControllerSettings EchoXR_Controls()
{
	std::lock_guard<std::mutex> lock(g_lock);
	const ULONGLONG now = GetTickCount64();
	if (g_checked && now - g_lastCheck < 500)
		return g_settings;
	g_lastCheck = now;

	const bool first = !g_checked;
	if (first)
		g_path = SettingsPath();
	g_checked = true;

	WIN32_FILE_ATTRIBUTE_DATA a;
	const bool present = GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &a) != 0;
	const ULONGLONG size = present ? ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow : 0;
	const bool changed = present != g_present ||
		(present && (CompareFileTime(&a.ftLastWriteTime, &g_written) != 0 || size != g_size));
	if (!first && !changed)
		return g_settings;

	if (!present)
	{
		g_settings = echoxr::ControllerSettings();
		g_present = false;
		Describe(g_settings, first ? "defaults, no EchoXR.txt" : "defaults, EchoXR.txt is gone");
		return g_settings;
	}

	std::string text;
	if (!ReadText(g_path, text))
		return g_settings;      // being replaced: the next check reads it

	std::vector<std::string> notes;
	g_settings = echoxr::ParseControllerSettings(text, &notes);
	g_present = true;
	g_written = a.ftLastWriteTime;
	g_size = size;
	const std::string where = Utf8(g_path);
	Describe(g_settings, where.c_str());
	for (const std::string& n : notes)
		EchoXR_Log("  EchoXR.txt: %s", n.c_str());
	return g_settings;
}
