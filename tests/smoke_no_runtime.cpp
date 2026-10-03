// Loads the built LibOVRRT64_1.dll (argv[1]) with no OpenXR runtime to talk to (ctest points
// XR_RUNTIME_JSON at a file that doesn't exist). ovr_Initialize has to fail with an error,
// not crash or hang, and ovr_Shutdown has to be safe afterwards.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <OVR_CAPI.h>
#include <cstdio>

typedef ovrResult (*Initialize)(const ovrInitParams*);
typedef void (*Shutdown)();

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: smoke_no_runtime <LibOVRRT64_1.dll>\n");
		return 2;
	}
	HMODULE dll = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!dll)
	{
		std::printf("FAIL: couldn't load %s (error %lu)\n", argv[1], GetLastError());
		return 1;
	}
	auto init = (Initialize)GetProcAddress(dll, "ovr_Initialize");
	auto shutdown = (Shutdown)GetProcAddress(dll, "ovr_Shutdown");
	if (!init || !shutdown)
	{
		std::printf("FAIL: ovr_Initialize or ovr_Shutdown not exported\n");
		return 1;
	}
	ovrInitParams params = {};
	params.Flags = ovrInit_RequestVersion;
	params.RequestedMinorVersion = 55;   // what Echo asks for
	ovrResult result = init(&params);
	std::printf("ovr_Initialize returned %d\n", result);
	if (OVR_SUCCESS(result))
	{
		std::printf("FAIL: ovr_Initialize succeeded with no OpenXR runtime\n");
		shutdown();
		return 1;
	}
	shutdown();
	std::printf("ok: failed cleanly\n");
	return 0;
}
