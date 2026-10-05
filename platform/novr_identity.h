// novr_identity.h — per-machine identity for EchoXR's Platform SDK stand-in.
// By marshmallow-mia (first written for NoOvrEchoVR_on_Linux).
//
// Goal: give every machine its own stable EchoVR/Nakama identity WITHOUT any
// Steam/Oculus dependency, derived from something that is "always there".
//
// Anchor  = under Wine, the primary PHYSICAL NIC's MAC address (read from /sys via the
//           Z: drive), else /etc/machine-id; on Windows, the install's MachineGuid
//           (HKLM\SOFTWARE\Microsoft\Cryptography); the hostname last. The order keeps
//           every id a Linux player already has.
// Hashing = SHA-256 over a fixed salt + the anchor. SHA-256 is ONE-WAY, so the
//           published id/serial cannot be turned back into the MAC (base64 alone
//           is reversible and would leak it — we base64url the *digest*, never the
//           raw MAC). The salt also defeats brute-forcing the 48-bit MAC space.
// Outputs = app-scoped id + org-scoped id (uint64) and a "NOVR-<base64url>" serial.
//
// Header-only: every function is static. Requires <windows.h>.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

namespace novr_id {

// Fixed project salt. NOT a secret (it ships in the binary) but it stops a casual
// server-side party from brute-forcing the small MAC space against a bare hash.
static const char kSalt[] = "NOVR.identity.v1//4b0c9f3d-stub-salt";

// ---------------- SHA-256 (compact, public-domain style) ----------------
struct Sha256 {
  uint32_t s[8]; uint64_t len; uint8_t buf[64]; int n;
};
static inline uint32_t Ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
static inline void ShaInit(Sha256* c) {
  static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                 0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  for (int i = 0; i < 8; i++) c->s[i] = iv[i];
  c->len = 0; c->n = 0;
}
static inline void ShaBlock(Sha256* c, const uint8_t* p) {
  static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = (p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = Ror(w[i-15],7) ^ Ror(w[i-15],18) ^ (w[i-15]>>3);
    uint32_t s1 = Ror(w[i-2],17) ^ Ror(w[i-2],19) ^ (w[i-2]>>10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  uint32_t a=c->s[0],b=c->s[1],cc=c->s[2],d=c->s[3],e=c->s[4],f=c->s[5],g=c->s[6],h=c->s[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = Ror(e,6)^Ror(e,11)^Ror(e,25);
    uint32_t ch = (e&f)^(~e&g);
    uint32_t t1 = h + S1 + ch + k[i] + w[i];
    uint32_t S0 = Ror(a,2)^Ror(a,13)^Ror(a,22);
    uint32_t maj = (a&b)^(a&cc)^(b&cc);
    uint32_t t2 = S0 + maj;
    h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
  }
  c->s[0]+=a; c->s[1]+=b; c->s[2]+=cc; c->s[3]+=d;
  c->s[4]+=e; c->s[5]+=f; c->s[6]+=g; c->s[7]+=h;
}
static inline void ShaUpdate(Sha256* c, const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data; c->len += len;
  while (len--) { c->buf[c->n++] = *p++; if (c->n == 64) { ShaBlock(c, c->buf); c->n = 0; } }
}
static inline void ShaFinal(Sha256* c, uint8_t out[32]) {
  uint64_t bits = c->len * 8; c->buf[c->n++] = 0x80;
  if (c->n > 56) { while (c->n < 64) c->buf[c->n++] = 0; ShaBlock(c, c->buf); c->n = 0; }
  while (c->n < 56) c->buf[c->n++] = 0;
  for (int i = 7; i >= 0; i--) c->buf[c->n++] = (uint8_t)(bits >> (i*8));
  ShaBlock(c, c->buf);
  for (int i = 0; i < 8; i++) { out[i*4]=(uint8_t)(c->s[i]>>24); out[i*4+1]=(uint8_t)(c->s[i]>>16);
                                out[i*4+2]=(uint8_t)(c->s[i]>>8); out[i*4+3]=(uint8_t)c->s[i]; }
}

// ---------------- base64url (no padding) ----------------
static inline void Base64Url(const uint8_t* in, int n, char* out, int cap) {
  static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  int o = 0, i = 0;
  while (i < n && o + 4 < cap) {
    uint32_t v = in[i] << 16;
    int rem = n - i;
    if (rem > 1) v |= in[i+1] << 8;
    if (rem > 2) v |= in[i+2];
    out[o++] = A[(v >> 18) & 63];
    out[o++] = A[(v >> 12) & 63];
    if (rem > 1) out[o++] = A[(v >> 6) & 63];
    if (rem > 2) out[o++] = A[v & 63];
    i += 3;
  }
  out[o < cap ? o : cap - 1] = 0;
}

// ---------------- small Z:-drive file reader (Wine maps Z:\ -> /) ----------------
static inline int ReadZ(const char* unixPath, char* out, int cap) {
  char wp[600]; int k = 0; wp[k++] = 'Z'; wp[k++] = ':';
  for (const char* p = unixPath; *p && k < 596; ++p) wp[k++] = (*p == '/') ? '\\' : *p;
  wp[k] = 0;
  HANDLE h = CreateFileA(wp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return 0;
  DWORD got = 0; BOOL ok = ReadFile(h, out, cap - 1, &got, nullptr); CloseHandle(h);
  if (!ok) { out[0] = 0; return 0; }
  out[got] = 0; return (int)got;
}
static inline bool ZExists(const char* unixPath) {
  char wp[600]; int k = 0; wp[k++] = 'Z'; wp[k++] = ':';
  for (const char* p = unixPath; *p && k < 596; ++p) wp[k++] = (*p == '/') ? '\\' : *p;
  wp[k] = 0;
  return GetFileAttributesA(wp) != INVALID_FILE_ATTRIBUTES;
}

// Virtual / non-hardware interface name prefixes we must never anchor on (their
// MACs are randomly generated and change on daemon restart).
static inline bool IsVirtualIface(const char* n) {
  static const char* pre[] = {"lo","docker","virbr","br-","veth","vmnet","tap","tun",
                              "bond","dummy","sit","wg","tailscale","zt","ham","vboxnet"};
  for (size_t i = 0; i < sizeof(pre)/sizeof(pre[0]); i++) {
    const char* p = pre[i]; int j = 0; while (p[j] && n[j] == p[j]) j++;
    if (!p[j]) return true;
  }
  return false;
}
// Parse "aa:bb:cc:dd:ee:ff" -> 6 bytes. Returns true on a non-zero MAC.
static inline bool ParseMac(const char* s, uint8_t mac[6]) {
  int v = 0, nb = 0; uint8_t cur = 0, allz = 0; bool half = false;
  for (; *s && nb < 6; s++) {
    char c = *s; int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else if (c == ':') { continue; }
    else break;
    cur = (uint8_t)((cur << 4) | d); half = !half;
    if (!half) { mac[nb++] = cur; allz |= cur; cur = 0; }
    (void)v;
  }
  return nb == 6 && allz != 0;
}

// Find the primary physical NIC MAC. Prefers a permanent (addr_assign_type==0)
// hardware MAC; picks deterministically (lowest interface name) on ties.
static inline bool GetPrimaryMac(uint8_t mac[6]) {
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA("Z:\\sys\\class\\net\\*", &fd);
  if (h == INVALID_HANDLE_VALUE) return false;
  char bestName[64] = {0}; uint8_t bestMac[6]; int bestPerm = -1; bool found = false;
  do {
    const char* name = fd.cFileName;
    if (name[0] == '.' || IsVirtualIface(name)) continue;
    char path[256];
    // physical NICs expose a "device" link; pure-virtual ones do not.
    wsprintfA(path, "/sys/class/net/%s/device", name);
    if (!ZExists(path)) continue;
    wsprintfA(path, "/sys/class/net/%s/address", name);
    char buf[64] = {0}; if (ReadZ(path, buf, sizeof(buf)) <= 0) continue;
    uint8_t m[6]; if (!ParseMac(buf, m)) continue;
    wsprintfA(path, "/sys/class/net/%s/addr_assign_type", name);
    char at[16] = {0}; ReadZ(path, at, sizeof(at));
    int perm = (at[0] == '0') ? 1 : 0;   // permanent hardware MAC ranks first
    bool take = !found || perm > bestPerm ||
                (perm == bestPerm && lstrcmpA(name, bestName) < 0);
    if (take) { for (int i=0;i<6;i++) bestMac[i]=m[i]; lstrcpynA(bestName,name,sizeof(bestName));
                bestPerm = perm; found = true; }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  if (found) for (int i = 0; i < 6; i++) mac[i] = bestMac[i];
  return found;
}

// Windows' own id of this install (set up once, kept across renames and updates).
static inline bool ReadMachineGuid(char* out, int cap) {
  HKEY k;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0,
                    KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
  DWORD type = 0, n = (DWORD)cap - 1;
  LONG r = RegQueryValueExA(k, "MachineGuid", nullptr, &type, (LPBYTE)out, &n);
  RegCloseKey(k);
  if (r != ERROR_SUCCESS || type != REG_SZ || n < 8) return false;
  out[n < (DWORD)cap ? n : cap - 1] = 0;
  return out[0] != 0;
}

// Derive the per-user identity. Fills appId/orgId (both nonzero) and, if serialOut
// is non-null, a "NOVR-<base64url>" serial. Returns the anchor source for logging.
static inline const char* DeriveIdentity(unsigned long long* appId,
                                         unsigned long long* orgId,
                                         char* serialOut, int serialCap) {
  uint8_t seed[64]; int slen = 0; const char* src;
  uint8_t mac[6];
  char mid[64] = {0};
  if (GetPrimaryMac(mac)) {
    const char* tag = "mac:"; for (int i = 0; tag[i]; i++) seed[slen++] = tag[i];
    for (int i = 0; i < 6; i++) seed[slen++] = mac[i];
    src = "mac";
  } else if (ReadZ("/etc/machine-id", mid, sizeof(mid)) > 0 && mid[0]) {
    const char* tag = "mid:"; for (int i = 0; tag[i]; i++) seed[slen++] = tag[i];
    for (int i = 0; mid[i] && mid[i] != '\n' && slen < (int)sizeof(seed); i++) seed[slen++] = mid[i];
    src = "machine-id";
  } else if (ReadMachineGuid(mid, sizeof(mid))) {
    const char* tag = "guid:"; for (int i = 0; tag[i]; i++) seed[slen++] = tag[i];
    for (int i = 0; mid[i] && slen < (int)sizeof(seed); i++) seed[slen++] = mid[i];
    src = "machine-guid";
  } else {
    char host[40] = {0}; DWORD hn = sizeof(host);
    if (!GetComputerNameA(host, &hn) || !host[0]) lstrcpynA(host, "player", sizeof(host));
    const char* tag = "host:"; for (int i = 0; tag[i]; i++) seed[slen++] = tag[i];
    for (int i = 0; host[i] && slen < (int)sizeof(seed); i++) seed[slen++] = host[i];
    src = "hostname";
  }
  Sha256 c; ShaInit(&c);
  ShaUpdate(&c, kSalt, sizeof(kSalt) - 1);
  ShaUpdate(&c, seed, slen);
  uint8_t dg[32]; ShaFinal(&c, dg);

  unsigned long long a = 0, o = 0;
  for (int i = 0; i < 8; i++)  a = (a << 8) | dg[i];
  for (int i = 8; i < 16; i++) o = (o << 8) | dg[i];
  a &= 0x000FFFFFFFFFFFFFULL;            // keep within the xplatform-id range
  if (a == 0) a = 1;
  if (o == 0) o = 2;
  if (appId) *appId = a;
  if (orgId) *orgId = o;
  if (serialOut && serialCap > 6) {
    serialOut[0]='N'; serialOut[1]='O'; serialOut[2]='V'; serialOut[3]='R'; serialOut[4]='-';
    Base64Url(dg, 9, serialOut + 5, serialCap - 5);   // 9 bytes -> 12 base64url chars
  }
  return src;
}

} // namespace novr_id
