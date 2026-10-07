// Unit tests for xr/src/echoxr_policy.h and echoxr_common.h's game build table.
#include "echoxr_policy.h"
#include "echoxr_common.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) \
	do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static bool Has(const std::vector<const char*>& list, const char* name)
{
	for (const char* e : list)
		if (std::string(e) == name)
			return true;
	return false;
}

static int Count(const std::vector<const char*>& list, const char* name)
{
	int n = 0;
	for (const char* e : list)
		n += std::string(e) == name;
	return n;
}

// What Proton's wineopenxr offers for a Linux runtime with XR_KHR_vulkan_enable: the
// runtime's own list plus D3D11 and D3D12 (and the Win32 time conversion).
static const std::vector<std::string> kWineOffers = {
	"XR_KHR_vulkan_enable", "XR_KHR_opengl_enable", "XR_KHR_visibility_mask",
	"XR_KHR_composition_layer_depth", "XR_KHR_D3D11_enable", "XR_KHR_D3D12_enable",
	"XR_KHR_win32_convert_performance_counter_time", "XR_MND_headless" };

static void Extensions()
{
	using namespace echoxr;
	// Under Wine: one graphics API. Each of these would become XR_KHR_vulkan_enable.
	auto wine = ChooseExtensions(true, GraphicsApi::D3D12, kWineOffers);
	int graphics = Count(wine, "XR_KHR_D3D11_enable") + Count(wine, "XR_KHR_D3D12_enable") +
		Count(wine, "XR_KHR_vulkan_enable");
	CHECK(graphics == 1);
	CHECK(Has(wine, "XR_KHR_D3D12_enable"));
	CHECK(!Has(wine, "XR_KHR_opengl_enable"));
	CHECK(Has(wine, "XR_KHR_win32_convert_performance_counter_time"));
	CHECK(Has(wine, "XR_MND_headless"));
	CHECK(Has(wine, "XR_KHR_visibility_mask"));
	CHECK(!Has(wine, "XR_FB_color_space"));     // not offered, so not enabled

	// Windows: Revive's set, D3D11 required, the others when offered.
	auto win = ChooseExtensions(false, GraphicsApi::D3D11, { "XR_KHR_D3D11_enable", "XR_KHR_D3D12_enable",
		"XR_KHR_vulkan_enable", "XR_KHR_win32_convert_performance_counter_time" });
	CHECK(Has(win, "XR_KHR_D3D11_enable"));
	CHECK(Has(win, "XR_KHR_D3D12_enable"));
	CHECK(Has(win, "XR_KHR_vulkan_enable"));
	CHECK(!Has(win, "XR_KHR_opengl_enable"));

	// Required ones are always asked for, so a runtime without them fails with a clear error.
	auto bare = ChooseExtensions(true, GraphicsApi::D3D12, {});
	CHECK(Has(bare, "XR_KHR_D3D12_enable"));
	CHECK(bare.size() == RequiredExtensions(true, GraphicsApi::D3D12).size());

	// An event build under Wine: D3D11 alone.
	auto event = ChooseExtensions(true, GraphicsApi::D3D11, kWineOffers);
	CHECK(Has(event, "XR_KHR_D3D11_enable"));
	CHECK(!Has(event, "XR_KHR_D3D12_enable"));
	CHECK(Count(event, "XR_KHR_D3D11_enable") + Count(event, "XR_KHR_D3D12_enable") + Count(event, "XR_KHR_vulkan_enable") == 1);

	// No extension twice, on either platform.
	for (bool w : { true, false })
	{
		auto all = ChooseExtensions(w, w ? GraphicsApi::D3D12 : GraphicsApi::D3D11, kWineOffers);
		for (const char* e : all)
			CHECK(Count(all, e) == 1);
	}

	// The game's API: Windows always D3D11; under Wine the hint, else the exe's imports.
	CHECK(GameApi(false, "d3d12", false) == GraphicsApi::D3D11);
	CHECK(GameApi(true, nullptr, false) == GraphicsApi::D3D12);
	CHECK(GameApi(true, nullptr, true) == GraphicsApi::D3D11);
	CHECK(GameApi(true, "d3d12", true) == GraphicsApi::D3D12);
	CHECK(GameApi(true, "d3d11", false) == GraphicsApi::D3D11);
}

static void DepthFormats()
{
	using namespace echoxr;
	// D24S8 not offered (AMD through Proton): D32S8, which keeps the stencil.
	CHECK(NegotiateDepthFormat(kDxgiD24UnormS8, false, true) == kDxgiD32FloatS8X24);
	// Offered: unchanged.
	CHECK(NegotiateDepthFormat(kDxgiD24UnormS8, true, true) == kDxgiD24UnormS8);
	// Neither: unchanged (the runtime then says what's wrong).
	CHECK(NegotiateDepthFormat(kDxgiD24UnormS8, false, false) == kDxgiD24UnormS8);
	// Other formats are never touched.
	CHECK(NegotiateDepthFormat(29 /* R8G8B8A8_UNORM_SRGB */, false, true) == 29);
}

static void Layers()
{
	using namespace echoxr;
	CHECK(!BlendsByAlpha(kLayerEyeFov));
	CHECK(!BlendsByAlpha(kLayerEyeFovDepth));
	CHECK(!BlendsByAlpha(kLayerEyeMatrix));
	CHECK(BlendsByAlpha(3));   // quad
	CHECK(BlendsByAlpha(8));   // cylinder
	CHECK(BlendsByAlpha(10));  // cube
}

static void VrKeyDecisions()
{
	using namespace echoxr;
	CHECK(DecideVrKey(true, 0, false, false) == VrKey::Wait);    // Proton still at it
	CHECK(DecideVrKey(true, 1, true, true) == VrKey::Ready);     // Proton did it all
	CHECK(DecideVrKey(true, 1, false, false) == VrKey::SetUp);   // OpenVR fine, OpenXR not
	CHECK(DecideVrKey(true, 1, true, false) == VrKey::SetUp);
	CHECK(DecideVrKey(true, -1, false, false) == VrKey::SetUp);  // no OpenVR runtime
	CHECK(DecideVrKey(false, 0, false, false) == VrKey::SetUp);  // Proton's set-up didn't run
}

static void Adapters()
{
	struct Luid { unsigned low; int high; };
	std::vector<Luid> adapters = { { 1, 0 }, { 2, 0 }, { 3, 1 } };
	CHECK(echoxr::PickAdapter(adapters, Luid{ 2, 0 }) == 1);
	CHECK(echoxr::PickAdapter(adapters, Luid{ 3, 1 }) == 2);
	CHECK(echoxr::PickAdapter(adapters, Luid{ 9, 9 }) == 0);   // no match: the first one
	CHECK(echoxr::PickAdapter(std::vector<Luid>{}, Luid{ 1, 0 }) == -1);
}

// A minimal PE image of b: its timestamp, one section covering every patch, each patch's
// original bytes in place.
static std::string FakeExe(const echoxr::GameBuild& b)
{
	DWORD end = 0x2000;
	for (int i = 0; i < b.patchCount; ++i)
		end = b.patches[i].rva + 0x100 > end ? b.patches[i].rva + 0x100 : end;
	std::string pe(0x400 + end - 0x1000, '\0');
	pe[0] = 'M'; pe[1] = 'Z';
	*(DWORD*)&pe[0x3C] = 0x40;
	IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)&pe[0x40];
	nt->Signature = IMAGE_NT_SIGNATURE;
	nt->FileHeader.NumberOfSections = 1;
	nt->FileHeader.TimeDateStamp = b.timestamp;
	nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
	IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
	sec->VirtualAddress = 0x1000;
	sec->Misc.VirtualSize = sec->SizeOfRawData = end - 0x1000;
	sec->PointerToRawData = 0x400;
	for (int i = 0; i < b.patchCount; ++i)
		memcpy(&pe[0x400 + b.patches[i].rva - 0x1000], b.patches[i].expect, b.patches[i].expectLen);
	return pe;
}

static void GameBuilds()
{
	for (const echoxr::GameBuild& b : echoxr::kBuilds)
	{
		std::string exe = FakeExe(b), patched = exe;
		std::wstring err;
		CHECK(echoxr::FindBuild(echoxr::PeTimestamp(exe)) == &b);
		CHECK(echoxr::PatchExe(patched, b, err));
		for (int i = 0; i < b.patchCount; ++i)
			CHECK(!memcmp(&patched[echoxr::RvaToOffset(patched, b.patches[i].rva)], b.patches[i].with, b.patches[i].len));
		std::string again = patched;
		CHECK(!echoxr::PatchExe(again, b, err));                 // already patched: refused
		CHECK(err.find(L"already patched") != std::wstring::npos);
		std::string other = exe;
		other[echoxr::RvaToOffset(other, b.patches[0].rva)] ^= 0xFF;   // other bytes: refused
		CHECK(!echoxr::PatchExe(other, b, err));
	}
	CHECK(echoxr::FindBuild(0x12345678) == nullptr);
	CHECK(echoxr::PeTimestamp("not a PE") == 0);
}

int main()
{
	Extensions();
	GameBuilds();
	DepthFormats();
	Layers();
	VrKeyDecisions();
	Adapters();
	if (g_failures)
	{
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
