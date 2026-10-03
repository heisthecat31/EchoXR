// Unit tests for xr/src/echoxr_policy.h.
#include "echoxr_policy.h"

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
	auto wine = ChooseExtensions(true, kWineOffers);
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
	auto win = ChooseExtensions(false, { "XR_KHR_D3D11_enable", "XR_KHR_D3D12_enable",
		"XR_KHR_vulkan_enable", "XR_KHR_win32_convert_performance_counter_time" });
	CHECK(Has(win, "XR_KHR_D3D11_enable"));
	CHECK(Has(win, "XR_KHR_D3D12_enable"));
	CHECK(Has(win, "XR_KHR_vulkan_enable"));
	CHECK(!Has(win, "XR_KHR_opengl_enable"));

	// Required ones are always asked for, so a runtime without them fails with a clear error.
	auto bare = ChooseExtensions(true, {});
	CHECK(Has(bare, "XR_KHR_D3D12_enable"));
	CHECK(bare.size() == RequiredExtensions(true).size());

	// No extension twice, on either platform.
	for (bool w : { true, false })
	{
		auto all = ChooseExtensions(w, kWineOffers);
		for (const char* e : all)
			CHECK(Count(all, e) == 1);
	}

	CHECK(ProbeApi(true) == GraphicsApi::D3D12);
	CHECK(ProbeApi(false) == GraphicsApi::D3D11);
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

int main()
{
	Extensions();
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
