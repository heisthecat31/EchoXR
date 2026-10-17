// EchoXR's LibOVRPlatform64_1.dll: the Oculus Platform SDK, answered without Meta.
// By marshmallow-mia (first written for NoOvrEchoVR_on_Linux). EchoXR.exe puts EchoXR\ first
// on PATH and in LIBOVR_DLL_DIR, so the game loads this one instead of Meta's loader, which
// needs the Oculus service and an Oculus account (neither under Proton, nor without the
// Meta app).
//
// The real pnsovr.dll (the game's "OVR provider") imports LibOVRPlatform64_1.dll
// and uses it to obtain the logged-in Oculus user. The real platform DLL talks to
// the Oculus service; with no service / no Oculus account it returns user id 0, so
// pnsovr never produces a user, the game never creates an NSUSER, never logs
// "[NETGAME] Logging in...", ends multiplayer and unloads the level (the +0x1334E00
// use-after-free crash). This shim returns a FAKE logged-in user with no service,
// so pnsovr produces a valid identity and the game proceeds to the EchoVRCE/Nakama
// network login configured in _local\config.json.
//
// Login flow inside pnsovr (RE'd): RadPluginMain calls ovr_GetLoggedInUserID (sync,
// app-scoped id) then submits async ovr_User_GetLoggedInUser / ovr_User_GetOrgScopedID
// / ovr_User_GetAccessToken; per frame it drains ovr_PopMessage and dispatches by
// request id to handlers that read ovr_Message_GetUser/GetOrgScopedID/GetString and
// ovr_User_GetOculusID / ovr_OrgScopedID_GetID. We synthesize all of that.
//
// Every other LibOVRPlatform export is aliased to Stub_Noop in the .def so pnsovr's
// ~150 imports (and runtime GetProcAddress lookups) all resolve.

#include <windows.h>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <mmdeviceapi.h>   // WASAPI capture for the real microphone (VOIP)
#include <audioclient.h>
#include "novr_identity.h"   // per-machine id/serial (salted SHA-256, base64url)

// ---- fake identity (PER-MACHINE) ----
// EchoVR/Nakama identify a player by the Oculus xplatform id (these numeric ids) + the HMD serial.
// A SHARED hardcoded value makes everyone collide on ONE account, so these are only NEUTRAL
// PLACEHOLDERS: InitFakeIdentity() (called from DllMain) OVERWRITES them at load with ids
// derived from the machine (novr_identity.h: its physical NIC's MAC under Wine, its Windows
// MachineGuid otherwise, salted and SHA-256 hashed), so each player has their own identity and
// nothing personal is baked in or published. The Steam persona, when there is one, is only
// the display name.
static unsigned long long kAppScopedId = 1ULL;          // neutral placeholder; set per-user in InitFakeIdentity()
static unsigned long long kOrgScopedId = 2ULL;          // neutral placeholder; set per-user in InitFakeIdentity()
static char               kOculusId[64] = "player";     // neutral placeholder; set per-user in InitFakeIdentity()

// ---- minimal logging: platform.log beside this DLL (EchoXR\, with launcher.log and runtime.log) ----
static void PlatLog(const char* fmt, ...) {
  static wchar_t path[MAX_PATH] = {0};
  if (!path[0]) {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&PlatLog), &self)) return;
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { path[0] = 0; return; }
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash || (slash - path) + 14 >= MAX_PATH) { path[0] = 0; return; }
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"platform.log");
  }
  FILE* f = _wfopen(path, L"a");
  if (!f) return;
  va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
  fprintf(f, "\n"); fclose(f);
}

// ---- fake handle objects ----
struct FakeUser  { unsigned long long id; const char* oculusId; const char* displayName; };
struct FakeOrgId { unsigned long long id; };

// ovrMessageType — values are routed by request-id inside pnsovr, so exact SDK
// constants are not required for dispatch; we tag each message for our own accessors.
enum MsgKind { MK_INIT = 1, MK_USER, MK_ORGID, MK_TOKEN, MK_FRIENDS, MK_PROOF, MK_ENTITLE };

struct FakeMsg {
  bool               inUse;
  unsigned long long reqId;
  int                kind;
  bool               isError;
  FakeUser*          user;
  FakeOrgId*         orgId;
  const char*        str;
};

static FakeUser  g_user  = { kAppScopedId, kOculusId, kOculusId };
static FakeOrgId g_org   = { kOrgScopedId };

static CRITICAL_SECTION g_cs;
static bool             g_csInit = false;
static FakeMsg          g_pool[64] = {};
static FakeMsg*         g_queue[64] = {};
static int              g_qHead = 0, g_qTail = 0;
static unsigned long long g_nextReq = 1;
static bool             g_platInit = false;

static void EnsureCs() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } }

static FakeMsg* AllocMsg() {
  for (int i = 0; i < 64; i++) if (!g_pool[i].inUse) { g_pool[i] = FakeMsg{}; g_pool[i].inUse = true; return &g_pool[i]; }
  return nullptr;
}

// Queue a message for an async request and return its request id.
static unsigned long long Submit(int kind, FakeUser* u, FakeOrgId* o, const char* s) {
  EnsureCs(); EnterCriticalSection(&g_cs);
  unsigned long long req = g_nextReq++;
  FakeMsg* m = AllocMsg();
  if (m) {
    m->reqId = req; m->kind = kind; m->isError = false;
    m->user = u; m->orgId = o; m->str = s;
    g_queue[g_qTail] = m; g_qTail = (g_qTail + 1) % 64;
  }
  LeaveCriticalSection(&g_cs);
  PlatLog("Submit kind=%d req=%llu", kind, req);
  return req;
}

extern "C" {

// ---- generic no-op for every other platform export (rax=0 is ABI-safe in x64) ----
__declspec(dllexport) long long Stub_Noop() { return 0; }

// ---- platform init / status ----
__declspec(dllexport) int  ovr_PlatformInitializeWindows(const char*) { g_platInit = true; PlatLog("PlatformInitializeWindows -> Success"); return 0; /*ovrPlatformInitialize_Success*/ }
__declspec(dllexport) unsigned long long ovr_PlatformInitializeWindowsAsync(const char*) { g_platInit = true; PlatLog("PlatformInitializeWindowsAsync"); return Submit(MK_INIT, nullptr, nullptr, nullptr); }
// The Oculus SDK ships several init spellings; this build only knew the two above, leaving the
// rest as Stub_Noop. pnsovr resolves init at runtime and may pick one of these — under Wine it
// took a Stub_Noop path (returned 0 = a bogus request id) and reported "Unknown". Implement the
// other variants too so whichever pnsovr calls actually initializes us.
__declspec(dllexport) int  ovr_PlatformInitializeWindowsAsynchronous(const char*) { g_platInit = true; PlatLog("PlatformInitializeWindowsAsynchronous -> Success"); return 0; }
__declspec(dllexport) int  ovr_PlatformInitializeWithAccessToken(unsigned long long, const char*) { g_platInit = true; PlatLog("PlatformInitializeWithAccessToken -> Success"); return 0; }
__declspec(dllexport) int  ovr_Platform_InitializeStandaloneOculus(const void*) { g_platInit = true; PlatLog("Platform_InitializeStandaloneOculus -> Success"); return 0; }
// pnsovr reads the async-init outcome via ovr_PlatformInitialize_GetResult(message) — return 0
// (ovrPlatformInitialize_Success) so the async path also succeeds.
__declspec(dllexport) int  ovr_PlatformInitialize_GetResult(void*) { PlatLog("PlatformInitialize_GetResult -> 0 (Success)"); return 0; }
// Diagnostic + safety: pnsovr formats the init result via this. Log the exact code it got so we
// can see what it considered the result, and return a non-"Unknown" string.
__declspec(dllexport) const char* ovrPlatformInitializeResult_ToString(int code) {
  PlatLog("ovrPlatformInitializeResult_ToString(code=%d)", code);
  return code == 0 ? "Success" : "Initialized";
}
__declspec(dllexport) bool ovr_IsPlatformInitialized() { PlatLog("IsPlatformInitialized -> %d", g_platInit ? 1 : 0); return g_platInit; }

// pnsovr (CheckEntitlement) calls this during init. Real signature returns an async ovrRequest;
// pnsovr then pops the message and treats "not an error" as entitled. Queue a non-error message.
__declspec(dllexport) unsigned long long ovr_Entitlement_GetIsViewerEntitled() { PlatLog("Entitlement_GetIsViewerEntitled"); return Submit(MK_ENTITLE, nullptr, nullptr, nullptr); }

// ---- key/value pairs and ids (header inlines in later SDKs; the event builds' pnsovr imports
// them, e.g. for ovr_Room_UpdateDataStore, and dereferenced the stub's NULL) ----
struct KeyValuePair {            // ovrKeyValuePair, 40 bytes
  const char* key;
  int valueType;                 // 0 string, 1 int, 2 double
  const char* stringValue;
  int intValue;
  double doubleValue;
};
static_assert(sizeof(KeyValuePair) == 40, "ovrKeyValuePair layout");
__declspec(dllexport) KeyValuePair ovrKeyValuePair_makeString(const char* key, const char* value) {
  KeyValuePair kv = {}; kv.key = key; kv.valueType = 0; kv.stringValue = value; return kv;
}
__declspec(dllexport) KeyValuePair ovrKeyValuePair_makeInt(const char* key, int value) {
  KeyValuePair kv = {}; kv.key = key; kv.valueType = 1; kv.intValue = value; return kv;
}
__declspec(dllexport) KeyValuePair ovrKeyValuePair_makeDouble(const char* key, double value) {
  KeyValuePair kv = {}; kv.key = key; kv.valueType = 2; kv.doubleValue = value; return kv;
}
// ovrID (an unsigned 64-bit id) from its decimal text.
__declspec(dllexport) bool ovrID_FromString(unsigned long long* out, const char* text) {
  if (!out || !text || !*text) return false;
  unsigned long long v = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (unsigned long long)(*p - '0');
  }
  *out = v;
  return true;
}

// ---- synchronous logged-in id ----
__declspec(dllexport) unsigned long long ovr_GetLoggedInUserID() { PlatLog("GetLoggedInUserID -> %llu", kAppScopedId); return kAppScopedId; }

// ---- async user requests ----
__declspec(dllexport) unsigned long long ovr_User_GetLoggedInUser() { return Submit(MK_USER, &g_user, nullptr, nullptr); }
__declspec(dllexport) unsigned long long ovr_User_GetOrgScopedID(unsigned long long /*userId*/) { return Submit(MK_ORGID, nullptr, &g_org, nullptr); }
// The event builds' pnsovr asks for the token again as soon as it has one (a refresh loop that
// the real service answers slowly). Answering every request at once made it spin hundreds of
// thousands of times a minute and starved the game's login. The first is answered at once;
// a refresh is answered 5 s later (ovr_PopMessage hands it out when due).
static unsigned long long g_tokenReq = 0;      // a refresh waiting to be answered
static ULONGLONG g_tokenDue = 0;
__declspec(dllexport) unsigned long long ovr_User_GetAccessToken() {
  static volatile LONG s_asked = 0;
  if (InterlockedIncrement(&s_asked) == 1) { PlatLog("ovr_User_GetAccessToken"); return Submit(MK_TOKEN, nullptr, nullptr, "offline.fake.token"); }
  EnsureCs(); EnterCriticalSection(&g_cs);
  unsigned long long req = g_nextReq++;
  g_tokenReq = req;
  g_tokenDue = GetTickCount64() + 5000;
  LeaveCriticalSection(&g_cs);
  if (s_asked == 2) PlatLog("ovr_User_GetAccessToken again: refreshes are answered after 5 s");
  return req;
}

__declspec(dllexport) unsigned long long ovr_User_GetLoggedInUserFriends() { return Submit(MK_FRIENDS, nullptr, nullptr, nullptr); }
// The login may need the Oculus USER PROOF (a nonce). Real pnsovr imports
// ovr_User_GetUserProof; if we leave it a no-op the game can spin waiting for the proof
// message that never arrives. Deliver a fake nonce (EchoVRCE/Nakama uses discord auth, not
// the Oculus proof, so the value should pass).
__declspec(dllexport) unsigned long long ovr_User_GetUserProof() { PlatLog("ovr_User_GetUserProof"); return Submit(MK_PROOF, nullptr, nullptr, "0000000000000000000000000000000000000000"); }
__declspec(dllexport) void* ovr_Message_GetUserProof(void* m) { return m; }  // proof handle = the message
__declspec(dllexport) const char* ovr_UserProof_GetNonce(void* proof) { return proof ? ((FakeMsg*)proof)->str : ""; }

// ---- message pump ----
__declspec(dllexport) void* ovr_PopMessage() {
  EnsureCs(); EnterCriticalSection(&g_cs);
  void* r = nullptr;
  if (g_qHead != g_qTail) { r = g_queue[g_qHead]; g_qHead = (g_qHead + 1) % 64; }
  else if (g_tokenReq && GetTickCount64() >= g_tokenDue) {
    FakeMsg* m = AllocMsg();                  // a token refresh that is due
    if (m) { m->reqId = g_tokenReq; m->kind = MK_TOKEN; m->isError = false; m->user = nullptr; m->orgId = nullptr; m->str = "offline.fake.token"; r = m; }
    g_tokenReq = 0;
  }
  LeaveCriticalSection(&g_cs);
  // Diagnostic: count poll rate. A HUGE count with the login stuck = the game is
  // spin-waiting on an OVR async message we are not delivering.
  static volatile long s_pop = 0; long n = (long)InterlockedIncrement((volatile LONG*)&s_pop);
  if (r) PlatLog("PopMessage -> msg reqId=%llu kind=%d (poll #%ld)", ((FakeMsg*)r)->reqId, ((FakeMsg*)r)->kind, n);
  else if ((n % 2000000) == 0) PlatLog("PopMessage poll #%ld (empty)", n);
  return r;
}
__declspec(dllexport) void ovr_FreeMessage(void* m) { if (m) ((FakeMsg*)m)->inUse = false; }

__declspec(dllexport) int  ovr_Message_GetType(void* m) { return m ? ((FakeMsg*)m)->kind : 0; }
__declspec(dllexport) unsigned long long ovr_Message_GetRequestID(void* m) { return m ? ((FakeMsg*)m)->reqId : 0; }
__declspec(dllexport) bool ovr_Message_IsError(void* m) { return m ? ((FakeMsg*)m)->isError : true; }
__declspec(dllexport) void* ovr_Message_GetUser(void* m) { return m ? (void*)((FakeMsg*)m)->user : nullptr; }
__declspec(dllexport) void* ovr_Message_GetOrgScopedID(void* m) { return m ? (void*)((FakeMsg*)m)->orgId : nullptr; }
__declspec(dllexport) const char* ovr_Message_GetString(void* m) { return m ? ((FakeMsg*)m)->str : ""; }

// ---- microphone / voip ----
// EchoVR pulls VOIP mic audio through the Oculus Platform mic API ("Using OVR provider for mic
// input"): Create -> Start -> per-frame GetNumSamplesAvailable + ReadData(float* mono 48kHz) ->
// Stop/Destroy. The old stub returned 0 samples => silent mic. Here we REALLY capture the host
// microphone via WASAPI (Wine routes the default capture endpoint to the pipewire default source),
// downmix to mono float @48kHz, and serve it through a ring buffer. Fails safe: if WASAPI init
// fails the ring stays empty and behavior is identical to the old silent stub (no regression).

// GUIDs defined locally to avoid cross-compile link deps on uuid libs.
static const GUID CLSID_MMDeviceEnumerator_ = { 0xBCDE0395,0xE52F,0x467C,{0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E} };
static const GUID IID_IMMDeviceEnumerator_  = { 0xA95664D2,0x9614,0x4F35,{0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6} };
static const GUID IID_IAudioClient_         = { 0x1CB9AD4C,0xDBFA,0x4C32,{0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2} };
static const GUID IID_IAudioCaptureClient_  = { 0xC8ADBD64,0xE71E,0x48A0,{0xA4,0xDE,0x18,0x5C,0x39,0x5C,0xD3,0x17} };

#define MIC_RING 48000      // 1s @48kHz mono
static float            g_micRing[MIC_RING];
static volatile LONG    g_micHead = 0, g_micTail = 0;   // head=write, tail=read (sample indices mod RING)
static CRITICAL_SECTION g_micCs;
static bool             g_micCsInit = false;
static volatile LONG    g_micRun = 0;
static HANDLE           g_micThread = nullptr;

static void MicPush(const float* s, int n) {
  EnterCriticalSection(&g_micCs);
  for (int i = 0; i < n; i++) {
    int next = (g_micHead + 1) % MIC_RING;
    if (next == g_micTail) g_micTail = (g_micTail + 1) % MIC_RING;  // overflow: drop oldest
    g_micRing[g_micHead] = s[i];
    g_micHead = next;
  }
  LeaveCriticalSection(&g_micCs);
}
static int MicAvail() {
  EnterCriticalSection(&g_micCs);
  int n = (g_micHead - g_micTail + MIC_RING) % MIC_RING;
  LeaveCriticalSection(&g_micCs);
  return n;
}
static int MicPop(float* out, int want) {
  EnterCriticalSection(&g_micCs);
  int got = 0;
  while (got < want && g_micTail != g_micHead) { out[got++] = g_micRing[g_micTail]; g_micTail = (g_micTail + 1) % MIC_RING; }
  LeaveCriticalSection(&g_micCs);
  return got;
}

static DWORD WINAPI MicCaptureThread(LPVOID) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator* en = nullptr;
  IMMDevice* dev = nullptr;
  IAudioClient* ac = nullptr;
  IAudioCaptureClient* cap = nullptr;
  WAVEFORMATEX* wf = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator_, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator_, (void**)&en);
  if (FAILED(hr) || !en) { PlatLog("mic: CoCreateInstance enum failed hr=%lx", (unsigned long)hr); goto done; }
  hr = en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
  if (FAILED(hr) || !dev) { PlatLog("mic: no default capture endpoint hr=%lx", (unsigned long)hr); goto done; }
  hr = dev->Activate(IID_IAudioClient_, CLSCTX_ALL, nullptr, (void**)&ac);
  if (FAILED(hr) || !ac) { PlatLog("mic: Activate IAudioClient failed hr=%lx", (unsigned long)hr); goto done; }
  hr = ac->GetMixFormat(&wf);
  if (FAILED(hr) || !wf) { PlatLog("mic: GetMixFormat failed hr=%lx", (unsigned long)hr); goto done; }
  {
    int ch = wf->nChannels, sr = wf->nSamplesPerSec, bps = wf->wBitsPerSample;
    bool isFloat = (wf->wFormatTag == 3 /*IEEE_FLOAT*/) ||
                   (wf->wFormatTag == 0xFFFE /*EXTENSIBLE*/ && ((WAVEFORMATEXTENSIBLE*)wf)->SubFormat.Data1 == 3);
    PlatLog("mic: device fmt ch=%d sr=%d bps=%d float=%d", ch, sr, bps, isFloat ? 1 : 0);
    REFERENCE_TIME dur = 2000000;  // 200ms buffer
    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, dur, 0, wf, nullptr);
    if (FAILED(hr)) { PlatLog("mic: IAudioClient.Initialize failed hr=%lx", (unsigned long)hr); goto done; }
    hr = ac->GetService(IID_IAudioCaptureClient_, (void**)&cap);
    if (FAILED(hr) || !cap) { PlatLog("mic: GetService capture failed hr=%lx", (unsigned long)hr); goto done; }
    ac->Start();
    PlatLog("mic: capture started");
    // Resample ratio device->48000 (linear). pipewire default is 48k so ratio is usually 1.
    double step = (double)sr / 48000.0; double phase = 0.0;
    static float mono[9600];
    while (g_micRun) {
      UINT32 pkt = 0;
      if (FAILED(cap->GetNextPacketSize(&pkt)) ) { Sleep(5); continue; }
      if (pkt == 0) { Sleep(5); continue; }
      BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
      if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) { Sleep(5); continue; }
      int outN = 0;
      for (UINT32 f = 0; f < frames && outN < 9600; ) {
        // downmix this frame to mono float
        float m = 0.0f;
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) m = 0.0f;
        else if (isFloat && bps == 32) { const float* s = (const float*)(data + (size_t)f*ch*4); for (int c=0;c<ch;c++) m += s[c]; m /= ch; }
        else if (bps == 16)            { const short* s = (const short*)(data + (size_t)f*ch*2); for (int c=0;c<ch;c++) m += s[c]/32768.0f; m /= ch; }
        else if (bps == 32)            { const int* s = (const int*)(data + (size_t)f*ch*4); for (int c=0;c<ch;c++) m += s[c]/2147483648.0f; m /= ch; }
        mono[outN++] = m;
        // advance source by resample step
        phase += step; while (phase >= 1.0 && f < frames) { f++; phase -= 1.0; }
      }
      cap->ReleaseBuffer(frames);
      if (outN > 0) {
        MicPush(mono, outN);
        // Diagnostic: log the captured level ~once/sec so we can tell "capture is silent"
        // (upstream Steam Link mic issue) apart from "read API drops the samples".
        static float dbgPeak = 0.0f; static int dbgCount = 0;
        for (int i = 0; i < outN; i++) { float a = mono[i] < 0 ? -mono[i] : mono[i]; if (a > dbgPeak) dbgPeak = a; }
        dbgCount += outN;
        if (dbgCount >= 48000) { PlatLog("mic: capture level peak=%.4f over %d samples", dbgPeak, dbgCount); dbgPeak = 0.0f; dbgCount = 0; }
      }
    }
    ac->Stop();
  }
done:
  if (wf) CoTaskMemFree(wf);
  if (cap) cap->Release();
  if (ac) ac->Release();
  if (dev) dev->Release();
  if (en) en->Release();
  CoUninitialize();
  PlatLog("mic: capture thread exit");
  return 0;
}

static char g_fakeMic[1024] = {};
__declspec(dllexport) void* ovr_Microphone_Create() {
  if (!g_micCsInit) { InitializeCriticalSection(&g_micCs); g_micCsInit = true; }
  PlatLog("ovr_Microphone_Create -> %p", (void*)&g_fakeMic);
  return (void*)&g_fakeMic;
}
__declspec(dllexport) void  ovr_Microphone_Destroy(void*) {
  InterlockedExchange(&g_micRun, 0);
}
__declspec(dllexport) unsigned long long ovr_Microphone_GetNumSamplesAvailable(void*) {
  return (unsigned long long)MicAvail();
}
__declspec(dllexport) unsigned long long ovr_Microphone_ReadData(void*, float* out, unsigned long long n) {
  if (!out || n == 0) return 0;
  return (unsigned long long)MicPop(out, (int)n);
}
// Standard Oculus mic read paths. EchoVR most likely pulls voice through GetPCM (int16) or
// GetPCMFloat rather than the non-standard ReadData; these were previously aliased to
// Stub_Noop (returned 0 = silent mic despite GetNumSamplesAvailable reporting data). All three
// drain the same capture ring so whichever the game calls works.
__declspec(dllexport) unsigned long long ovr_Microphone_GetPCM(void*, short* out, unsigned long long n) {
  if (!out || n == 0) return 0;
  static float tmp[9600];
  int want = (int)(n > 9600 ? 9600 : n);
  int got = MicPop(tmp, want);
  for (int i = 0; i < got; i++) {
    int v = (int)(tmp[i] * 32767.0f);
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    out[i] = (short)v;
  }
  return (unsigned long long)got;
}
__declspec(dllexport) unsigned long long ovr_Microphone_GetPCMFloat(void*, float* out, unsigned long long n) {
  if (!out || n == 0) return 0;
  return (unsigned long long)MicPop(out, (int)n);
}
__declspec(dllexport) unsigned long long ovr_Microphone_GetOutputBufferMaxSize(void*) {
  return (unsigned long long)MIC_RING;
}
__declspec(dllexport) void  ovr_Microphone_Start(void*) {
  if (!g_micCsInit) { InitializeCriticalSection(&g_micCs); g_micCsInit = true; }
  if (InterlockedExchange(&g_micRun, 1) == 0) {
    g_micHead = g_micTail = 0;
    g_micThread = CreateThread(nullptr, 0, MicCaptureThread, nullptr, 0, nullptr);
    PlatLog("ovr_Microphone_Start -> capture thread %p", (void*)g_micThread);
  }
}
__declspec(dllexport) void  ovr_Microphone_Stop(void*) {
  InterlockedExchange(&g_micRun, 0);
  PlatLog("ovr_Microphone_Stop");
}

// ---- enum *_ToString helpers ----
// The real pnsovr serializes OVR enum values into JSON request strings by calling
// LibOVRPlatform's `ovrXxx_ToString(enum)` and then `strlen()` on the result. There are
// ~55 such exports; left as Stub_Noop they return 0 = NULL, and the very first one pnsovr
// hits (ovrRoomJoinPolicy_ToString, during ovr_Room_CreateAndJoinPrivate2 serialization
// while building the post-login lobby) crashes in strlen(NULL) at pnsovr+0xACB60. A
// non-null C string is strictly safer than NULL for any of these. Stub_ToString is the
// generic fallback (aliased by every *_ToString in the .def); the few whose VALUE actually
// matters get a real mapping below.
__declspec(dllexport) const char* Stub_ToString() { return "Unknown"; }

// Canonical OVR RoomJoinPolicy names (proven crash site pnsovr+0x838F7). Win64: arg in ECX.
__declspec(dllexport) const char* ovrRoomJoinPolicy_ToString(int policy) {
  switch (policy) {
    case 0:  return "Unknown";
    case 1:  return "None";
    case 2:  return "Everyone";
    case 3:  return "FriendsOfMembers";
    case 4:  return "FriendsOfOwner";
    case 5:  return "InvitedUsers";
    default: return "Unknown";
  }
}

// ---- accessors ----
__declspec(dllexport) unsigned long long ovr_User_GetID(void* u) { return u ? ((FakeUser*)u)->id : 0; }
__declspec(dllexport) const char* ovr_User_GetOculusID(void* u) { return u ? ((FakeUser*)u)->oculusId : ""; }
__declspec(dllexport) const char* ovr_User_GetDisplayName(void* u) { return u ? ((FakeUser*)u)->displayName : ""; }
__declspec(dllexport) unsigned long long ovr_OrgScopedID_GetID(void* o) { return o ? ((FakeOrgId*)o)->id : 0; }

} // extern "C"

// --- pnsovr offline-login fix (Linux/Proton) ---------------------------------
// pnsovr's compiled-in Oculus loader stub (pnsovr+0x98a90) refuses to use our
// LibOVRPlatform unless the already-loaded DLL's path matches an EXPECTED path it
// builds from the Oculus runtime REGISTRY (HKLM\...\Oculus VR, LLC\OculusBase ->
// \Support\oculus-runtime\<ver>\). Under Wine there is no Oculus install, so the
// expected path is bogus, the _wcsicmp at pnsovr+0x98b53 fails, and the stub returns
// ovrPlatformInitialize_PreLoaded (-2) -> "Failed to initialize the Oculus VR Platform
// SDK" before it ever calls our init. The check is one branch:
//     pnsovr+0x98b5a:  74 27   je 0x...b83   ; paths match -> success
// Flip it to an unconditional jmp (EB 27) so the stub always takes the success path,
// then it proceeds to LoadLibrary + GetProcAddress + call our init/login normally.
// The same idea on the event builds' older pnsovr (2018-2019): its Platform SDK loader
// checks our DLL's Authenticode signature (WinVerifyTrust) and returns -1 unless it is
// Oculus-signed. Its success test is one branch too, flipped the same way.
//
// One entry per pnsovr build, by its PE timestamp: the branch's RVA, the bytes expected
// there (checked first; any other build is left alone) and the byte that replaces the first
// byte of the branch.
struct PnsovrPatch {
  DWORD timestamp;
  DWORD rva;
  unsigned char expect[6];
  int len;
  int at;                // offset of the byte to replace, within expect
  unsigned char with;
  const char* what;
};
static const PnsovrPatch kPnsovrPatches[] = {
  // The live build (34.4.631547.1): the preloaded DLL's path check (je -> jmp).
  { 0x6452DF9B, 0x98b5a, { 0x74, 0x27 }, 2, 0, 0xeb, "preload-path check" },
  // Halloween 2018 (16.0.253636.0): cmp r15d,1 ; je -> jmp, the signature check's success test.
  { 0x5BC7B836, 0x11fab, { 0x41, 0x83, 0xff, 0x01, 0x74, 0x11 }, 6, 4, 0xeb, "signature check" },
  // Christmas 2018 (18.3.268902.0): the same test.
  { 0x5C17F5CD, 0x13e5b, { 0x41, 0x83, 0xff, 0x01, 0x74, 0x11 }, 6, 4, 0xeb, "signature check" },
  // Summer 2019 (23.2.340872.0).
  { 0x5D388D2C, 0x150bb, { 0x41, 0x83, 0xff, 0x01, 0x74, 0x11 }, 6, 4, 0xeb, "signature check" },
  // Christmas 2017 (Echo Arena 6.0).
  { 0x5A394933, 0x1445b, { 0x41, 0x83, 0xff, 0x01, 0x74, 0x11 }, 6, 4, 0xeb, "signature check" },
  // Halloween 2017 (Echo Arena 1.76).
  { 0x59E8F7BF, 0x860b, { 0x41, 0x83, 0xff, 0x01, 0x74, 0x11 }, 6, 4, 0xeb, "signature check" },
};

static void PatchPnsovrLoaderCheck() {
  HMODULE h = GetModuleHandleW(L"pnsovr.dll");
  if (!h) h = GetModuleHandleW(L"pnsovr");
  if (!h) return;                                  // not mapped yet; caller retries
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)h;
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const unsigned char*)h + dos->e_lfanew);
  DWORD stamp = nt->FileHeader.TimeDateStamp;
  const PnsovrPatch* pp = nullptr;
  for (const PnsovrPatch& c : kPnsovrPatches) if (c.timestamp == stamp) pp = &c;
  if (!pp) { PlatLog("patch: pnsovr build 0x%08lX unknown -- not patched", stamp); return; }
  unsigned char* p = (unsigned char*)h + pp->rva;
  DWORD oldProt;
  if (!VirtualProtect(p, pp->len, PAGE_EXECUTE_READWRITE, &oldProt)) { PlatLog("patch: VirtualProtect failed"); return; }
  if (memcmp(p, pp->expect, pp->len) == 0) {
    p[pp->at] = pp->with;                          // je -> jmp : always take the success path
    PlatLog("patch: pnsovr+0x%lx %s bypassed (build 0x%08lX)", pp->rva + pp->at, pp->what, stamp);
  } else if (p[pp->at] == pp->with) {
    PlatLog("patch: pnsovr+0x%lx already patched", pp->rva + pp->at);
  } else {
    PlatLog("patch: pnsovr+0x%lx unexpected bytes -- NOT patched", pp->rva + pp->at);
  }
  VirtualProtect(p, pp->len, oldProt, &oldProt);
  FlushInstructionCache(GetCurrentProcess(), p, pp->len);
}

// pnsovr may not be enumerable via GetModuleHandle the instant our DllMain runs, so if
// the immediate attempt misses, retry briefly on a worker thread (starts after the
// loader lock is released; the loader stub doesn't run until RAD calls pnsovr's init).
static DWORD WINAPI PatchRetryThread(LPVOID) {
  for (int i = 0; i < 200; i++) {                  // ~20s safety window
    if (GetModuleHandleW(L"pnsovr.dll") || GetModuleHandleW(L"pnsovr")) { PatchPnsovrLoaderCheck(); return 0; }
    Sleep(100);
  }
  PlatLog("patch: pnsovr never appeared");
  return 0;
}

static bool TokEq(const char* b, DWORD i, DWORD got, const char* key, int n) {  // b[i]=='"', match key + closing '"'
  if (i + 1 + (DWORD)n >= got) return false;
  for (int q = 0; q < n; q++) if (b[i+1+q] != key[q]) return false;
  return b[i+1+n] == '"';
}
// Most-recent Steam user's PersonaName (Steam DISPLAY name) from the client's loginusers.vdf,
// filled into personaOut. Steam root from STEAM_COMPAT_CLIENT_INSTALL_PATH (Proton sets it), else
// derived from STEAM_COMPAT_DATA_PATH. Read via Wine's Z: drive. Empty if unavailable.
static void GetSteamIdentity(char* personaOut, int pcap) {
  if (personaOut && pcap) personaOut[0] = 0;
  char root[512] = {0};
  DWORD rn = GetEnvironmentVariableA("STEAM_COMPAT_CLIENT_INSTALL_PATH", root, sizeof(root));
  if (rn == 0 || rn >= sizeof(root)) {
    char cd[512] = {0};
    DWORD cn = GetEnvironmentVariableA("STEAM_COMPAT_DATA_PATH", cd, sizeof(cd));
    if (cn == 0 || cn >= sizeof(cd)) return;
    int slashes = 0, cut = -1;                        // strip "/steamapps/compatdata/<id>"
    for (int i = (int)cn - 1; i > 0; --i) if (cd[i] == '/' && ++slashes == 3) { cut = i; break; }
    if (cut <= 0) return;
    cd[cut] = 0; lstrcpynA(root, cd, sizeof(root));
  }
  char wp[600]; int k = 0; wp[k++] = 'Z'; wp[k++] = ':';
  for (const char* p = root; *p && k < 540; ++p) wp[k++] = (*p == '/') ? '\\' : *p;
  for (const char* p = "\\config\\loginusers.vdf"; *p && k < 598; ++p) wp[k++] = *p;
  wp[k] = 0;
  HANDLE h = CreateFileA(wp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  static char buf[16384]; DWORD got = 0; BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr); CloseHandle(h);
  if (!ok || got == 0) return; buf[got] = 0;
  // Walk blocks; track each block's PersonaName; capture it when the block has MostRecent "1".
  char curP[64] = {0}, mrP[64] = {0}; bool haveMr = false;
  for (DWORD i = 0; i < got; i++) {
    if (buf[i] != '"') continue;
    if (i + 5 < got && buf[i+1]=='7'&&buf[i+2]=='6'&&buf[i+3]=='5'&&buf[i+4]=='6') {       // "7656…" id = new user block
      DWORD j = i + 1; int d = 0;
      while (j < got && buf[j] >= '0' && buf[j] <= '9' && d < 20) { ++j; ++d; }
      if (j < got && buf[j] == '"' && d >= 16) { curP[0] = 0; i = j; continue; }
    }
    if (TokEq(buf, i, got, "PersonaName", 11)) {                                            // value -> curP
      DWORD j = i + 13; while (j < got && buf[j] != '"') j++;
      if (j < got) { j++; int c = 0; while (j < got && buf[j] != '"' && c < (int)sizeof(curP)-1) curP[c++] = buf[j++]; curP[c] = 0; }
      continue;
    }
    if (TokEq(buf, i, got, "MostRecent", 10)) {                                             // "1" -> capture block
      DWORD j = i + 12; while (j < got && buf[j] != '1' && buf[j] != '}' && buf[j] != '"') j++;
      if (j < got && buf[j] == '1') { lstrcpynA(mrP, curP, sizeof(mrP)); haveMr = true; }
      continue;
    }
  }
  if (personaOut && pcap) lstrcpynA(personaOut, (haveMr && mrP[0]) ? mrP : curP, pcap);
}
// Per-machine Oculus identity — the stable account key is machine-anchored (novr_id::DeriveIdentity),
// not Steam-derived: the same machine always maps to the same account. The Steam persona
// (PersonaName) supplies only the DISPLAY name when the Steam client is present.
static void InitFakeIdentity() {
  // ACCOUNT KEY (numeric xplatform ids) — anchored on the machine (see novr_identity.h),
  // salted and SHA-256 hashed (one-way; the anchor is never published). No Steam dependency:
  // the same machine always gets the same account.
  char serial[24] = {0};
  const char* src = novr_id::DeriveIdentity(&kAppScopedId, &kOrgScopedId, serial, sizeof(serial));

  // DISPLAY NAME — the gamertag other players see. A MAC can't be a name, so: Steam
  // persona if the client is present, else the hostname, else "player".
  char persona[64] = {0};
  GetSteamIdentity(persona, sizeof(persona));
  if (persona[0]) {
    lstrcpynA(kOculusId, persona, sizeof(kOculusId));
  } else {
    char host[40] = {0}; DWORD hn = sizeof(host);
    if (!GetComputerNameA(host, &hn) || !host[0]) lstrcpynA(host, "player", sizeof(host));
    lstrcpynA(kOculusId, host, sizeof(kOculusId));
  }

  g_user.id = kAppScopedId; g_user.oculusId = kOculusId; g_user.displayName = kOculusId;
  g_org.id  = kOrgScopedId;
  PlatLog("identity: src=%s name=%s serial=%s appId=%llu orgId=%llu",
          src, kOculusId, serial, kAppScopedId, kOrgScopedId);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    PlatLog("=== fake LibOVRPlatform64_1 loaded ===");
    InitFakeIdentity();
    if (GetModuleHandleW(L"pnsovr.dll") || GetModuleHandleW(L"pnsovr")) {
      PatchPnsovrLoaderCheck();                    // pnsovr already mapped (we're its import)
    } else {
      HANDLE t = CreateThread(nullptr, 0, PatchRetryThread, nullptr, 0, nullptr);
      if (t) CloseHandle(t);
    }
  }
  return TRUE;
}
