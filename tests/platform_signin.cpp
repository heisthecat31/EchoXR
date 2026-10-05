// EchoXR's LibOVRPlatform64_1.dll, loaded and asked as the game's pnsovr.dll asks it: the
// platform initialises without any service, a signed-in user comes back through the
// message queue with the same id the synchronous call gives, entitlement and the access
// token answer, any other export is a harmless no-op, and the id is the same at the next
// load (it is the machine's, not random).
#include <windows.h>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

typedef int (*InitFn)(const char*);
typedef unsigned long long (*U64Fn)();
typedef unsigned long long (*U64PtrFn)(void*);
typedef void* (*PtrFn)();
typedef void* (*PtrPtrFn)(void*);
typedef bool (*BoolPtrFn)(void*);
typedef const char* (*StrPtrFn)(void*);
typedef void (*VoidPtrFn)(void*);
typedef bool (*BoolFn)();
typedef long long (*NoopFn)();

template <class T> static T get(HMODULE m, const char* name) {
    T f = reinterpret_cast<T>(GetProcAddress(m, name));
    if (!f) { std::printf("FAILED: no export %s\n", name); ++failures; }
    return f;
}

static unsigned long long sign_in(const wchar_t* dll) {
    HMODULE m = LoadLibraryW(dll);
    CHECK(m != nullptr);
    if (!m) return 0;
    auto init = get<InitFn>(m, "ovr_PlatformInitializeWindows");
    auto inited = get<BoolFn>(m, "ovr_IsPlatformInitialized");
    auto loggedIn = get<U64Fn>(m, "ovr_GetLoggedInUserID");
    auto getUser = get<U64Fn>(m, "ovr_User_GetLoggedInUser");
    auto entitled = get<U64Fn>(m, "ovr_Entitlement_GetIsViewerEntitled");
    auto token = get<U64Fn>(m, "ovr_User_GetAccessToken");
    auto pop = get<PtrFn>(m, "ovr_PopMessage");
    auto reqId = get<U64PtrFn>(m, "ovr_Message_GetRequestID");
    auto isError = get<BoolPtrFn>(m, "ovr_Message_IsError");
    auto msgUser = get<PtrPtrFn>(m, "ovr_Message_GetUser");
    auto msgString = get<StrPtrFn>(m, "ovr_Message_GetString");
    auto userId = get<U64PtrFn>(m, "ovr_User_GetID");
    auto oculusId = get<StrPtrFn>(m, "ovr_User_GetOculusID");
    auto freeMsg = get<VoidPtrFn>(m, "ovr_FreeMessage");
    auto noop = get<NoopFn>(m, "ovr_Achievements_GetAllDefinitions");
    if (failures) return 0;

    CHECK(init("1369078409873402") == 0);
    CHECK(inited());
    unsigned long long id = loggedIn();
    CHECK(id != 0 && id != 1);

    unsigned long long rUser = getUser(), rEnt = entitled(), rTok = token();
    CHECK(rUser && rEnt && rTok && rUser != rEnt && rEnt != rTok);
    bool sawUser = false, sawEnt = false, sawTok = false;
    for (int i = 0; i < 16; ++i) {
        void* msg = pop();
        if (!msg) break;
        CHECK(!isError(msg));
        unsigned long long r = reqId(msg);
        if (r == rUser) {
            void* u = msgUser(msg);
            CHECK(u && userId(u) == id);
            CHECK(u && oculusId(u) && oculusId(u)[0]);
            sawUser = true;
        } else if (r == rEnt) {
            sawEnt = true;
        } else if (r == rTok) {
            CHECK(msgString(msg) && msgString(msg)[0]);
            sawTok = true;
        }
        freeMsg(msg);
    }
    CHECK(sawUser && sawEnt && sawTok);
    CHECK(pop() == nullptr);
    CHECK(noop() == 0);
    FreeLibrary(m);
    return id;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: platform_signin <LibOVRPlatform64_1.dll>\n"); return 2; }
    unsigned long long a = sign_in(argv[1]);
    unsigned long long b = sign_in(argv[1]);
    CHECK(a == b);
    if (failures) return 1;
    std::printf("platform sign-in ok\n");
    return 0;
}
