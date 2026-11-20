#include "test_harness.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include <windows.h>

#include "layout/geometry.h"
#include "rendering/renderer.h"
#include "rendering/renderer_provider.h"
#include "rendering/vulkan/vulkan_renderer.h"
#include "rendering/windows/direct2d_renderer.h"
#include "rendering/windows/software_renderer.h"
#include "theme/color.h"

namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Layout::Size;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::RendererBackend;
using XER::UI::Rendering::TextStyle;
using XER::UI::Rendering::Vulkan::VulkanRenderer;
using XER::UI::Rendering::Windows::Direct2DRenderer;
using XER::UI::Rendering::Windows::SoftwareRenderer;
using XER::UI::Theme::Color;

HWND CreateHiddenTestWindow(int w = 320, int h = 200) {
    static const wchar_t* kClass = L"XER_UI_PROVIDER_TEST";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    return CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"provider-test",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                           CW_USEDEFAULT, w, h, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

std::uint8_t BgraChannel(std::uint32_t px, int shift) {
    return static_cast<std::uint8_t>((px >> shift) & 0xFF);
}

} // namespace

// ---- name parsing / resolution ---------------------------------------

XER_UI_TEST(BackendNameRoundTrips) {
    for (const RendererBackend b :
         {RendererBackend::kAuto, RendererBackend::kSoftware,
          RendererBackend::kD2D, RendererBackend::kD3D11,
          RendererBackend::kVulkan}) {
        RendererBackend parsed = RendererBackend::kAuto;
        const auto name = XER::UI::Rendering::RendererBackendName(b);
        XER_UI_EXPECT_TRUE(
            XER::UI::Rendering::ParseRendererBackend(name, &parsed));
        XER_UI_EXPECT_TRUE(parsed == b);
    }
}

XER_UI_TEST(BackendParseAcceptsAliasesAndCase) {
    RendererBackend b = RendererBackend::kAuto;
    XER_UI_EXPECT_TRUE(
        XER::UI::Rendering::ParseRendererBackend("VULKAN", &b));
    XER_UI_EXPECT_TRUE(b == RendererBackend::kVulkan);
    XER_UI_EXPECT_TRUE(
        XER::UI::Rendering::ParseRendererBackend(" d2d ", &b));
    XER_UI_EXPECT_TRUE(b == RendererBackend::kD2D);
    XER_UI_EXPECT_TRUE(
        XER::UI::Rendering::ParseRendererBackend("Direct3D11", &b));
    XER_UI_EXPECT_TRUE(b == RendererBackend::kD3D11);
    XER_UI_EXPECT_TRUE(
        XER::UI::Rendering::ParseRendererBackend("cpu", &b));
    XER_UI_EXPECT_TRUE(b == RendererBackend::kSoftware);
}

XER_UI_TEST(BackendParseRejectsUnknown) {
    RendererBackend b = RendererBackend::kD2D;
    XER_UI_EXPECT_FALSE(
        XER::UI::Rendering::ParseRendererBackend("metal", &b));
    XER_UI_EXPECT_TRUE(b == RendererBackend::kD2D);  // untouched
    XER_UI_EXPECT_FALSE(
        XER::UI::Rendering::ParseRendererBackend("", &b));
    XER_UI_EXPECT_FALSE(
        XER::UI::Rendering::ParseRendererBackend("vulkan2", &b));
}

XER_UI_TEST(ResolveRendererBackendMapsEnv) {
    using XER::UI::Rendering::ResolveRendererBackend;
    XER_UI_EXPECT_TRUE(ResolveRendererBackend(nullptr) ==
                       RendererBackend::kAuto);
    XER_UI_EXPECT_TRUE(ResolveRendererBackend("") ==
                       RendererBackend::kAuto);
    XER_UI_EXPECT_TRUE(ResolveRendererBackend("definitely-not-a-gpu") ==
                       RendererBackend::kAuto);
    XER_UI_EXPECT_TRUE(ResolveRendererBackend("vulkan") ==
                       RendererBackend::kVulkan);
    XER_UI_EXPECT_TRUE(ResolveRendererBackend("software") ==
                       RendererBackend::kSoftware);
}

// ---- provider contract ------------------------------------------------

XER_UI_TEST(SoftwareProviderWorksHeadless) {
    auto renderer =
        XER::UI::Rendering::CreateRenderer(RendererBackend::kSoftware,
                                         nullptr);
    XER_UI_EXPECT_TRUE(renderer != nullptr);
    XER_UI_EXPECT_TRUE(dynamic_cast<SoftwareRenderer*>(renderer.get()) !=
                       nullptr);
}

XER_UI_TEST(D3D11BackendSelectsTheD3D11DeviceProvider) {
    // Documented alias: the D3D11 provider is the Direct2D renderer's
    // D3D11 device layer — no duplicated GPU pipeline.
    auto renderer =
        XER::UI::Rendering::CreateRenderer(RendererBackend::kD3D11,
                                         nullptr);
    XER_UI_EXPECT_TRUE(renderer != nullptr);
    XER_UI_EXPECT_TRUE(dynamic_cast<Direct2DRenderer*>(renderer.get()) !=
                       nullptr);
}

XER_UI_TEST(AutoAndExplicitBackendsInstantiateOrFallBack) {
    HWND hwnd = CreateHiddenTestWindow();
    XER_UI_EXPECT_TRUE(hwnd != nullptr);
    if (hwnd == nullptr) {
        return;
    }

    // kAuto always yields *some* renderer on Windows (software is the
    // terminal fallback).
    auto auto_r =
        XER::UI::Rendering::CreateRenderer(RendererBackend::kAuto, hwnd);
    XER_UI_EXPECT_TRUE(auto_r != nullptr);

    auto sw = XER::UI::Rendering::CreateRenderer(
        RendererBackend::kSoftware, hwnd);
    XER_UI_EXPECT_TRUE(sw != nullptr);
    XER_UI_EXPECT_TRUE(dynamic_cast<SoftwareRenderer*>(sw.get()) !=
                       nullptr);

    // Explicit vulkan: if an ICD exists this is a VulkanRenderer,
    // otherwise the factory silently falls back — either way non-null.
    auto vk = XER::UI::Rendering::CreateRenderer(RendererBackend::kVulkan,
                                               hwnd);
    XER_UI_EXPECT_TRUE(vk != nullptr);

    DestroyWindow(hwnd);
}

XER_UI_TEST(VulkanProviderHonorsContract) {
    // Vulkan-specific: VulkanRenderer::Create alone (not through the
    // fallback chain) must cleanly report unavailability as nullptr —
    // this is what makes the factory's graceful fallback possible.
    HWND hwnd = CreateHiddenTestWindow();
    XER_UI_EXPECT_TRUE(hwnd != nullptr);
    if (hwnd == nullptr) {
        return;
    }
    std::string error;
    auto vk = VulkanRenderer::Create(hwnd, &error);
    std::fprintf(stderr, "[vulkan-probe] %s\n",
                 vk != nullptr ? "renderer created (real Vulkan ICD)"
                               : error.c_str());
    if (vk == nullptr) {
        // Unavailable: must carry a diagnostic, must not have thrown.
        XER_UI_EXPECT_FALSE(error.empty());
    } else {
        // Available: contract smoke — a real frame must go through.
        vk->SetClearColor({0.0f, 0.0f, 0.0f, 1.0f});
        vk->BeginFrame(Rect{0.0f, 0.0f, 320.0f, 200.0f});
        vk->FillRect(Rect{10.0f, 10.0f, 100.0f, 50.0f},
                     {1.0f, 0.0f, 0.0f, 1.0f}, 0.0f);
        vk->EndFrame();
    }
    DestroyWindow(hwnd);
}

XER_UI_TEST(EnvSelectsSoftwareBackend) {
    HWND hwnd = CreateHiddenTestWindow();
    XER_UI_EXPECT_TRUE(hwnd != nullptr);
    if (hwnd == nullptr) {
        return;
    }
    _putenv("XER_UI_RENDERER=software");
    auto renderer = XER::UI::Rendering::CreateRendererFromEnv(hwnd);
    _putenv("XER_UI_RENDERER=");
    XER_UI_EXPECT_TRUE(renderer != nullptr);
    XER_UI_EXPECT_TRUE(dynamic_cast<SoftwareRenderer*>(renderer.get()) !=
                       nullptr);
    DestroyWindow(hwnd);
}

// ---- software renderer pixel smoke ------------------------------------

XER_UI_TEST(SoftwareFillRectPaintsPixels) {
    SoftwareRenderer renderer(nullptr);  // headless
    renderer.SetDpi(96);
    renderer.SetClearColor({0.0f, 0.0f, 0.0f, 1.0f});
    renderer.BeginFrame(Rect{0.0f, 0.0f, 64.0f, 64.0f});
    renderer.FillRect(Rect{16.0f, 16.0f, 32.0f, 32.0f},
                      {1.0f, 0.0f, 0.0f, 1.0f}, 0.0f);
    renderer.EndFrame();

    XER_UI_EXPECT_EQ(64, renderer.PixelWidth());
    XER_UI_EXPECT_EQ(64, renderer.PixelHeight());
    const std::uint32_t* px = renderer.Pixels();
    XER_UI_EXPECT_TRUE(px != nullptr);
    if (px == nullptr) {
        return;
    }
    const std::uint32_t center = px[32 * 64 + 32];
    // Red channel (BGRA -> bits 16..23) saturated, blue/green ~0.
    XER_UI_EXPECT_TRUE(BgraChannel(center, 16) > 200);
    XER_UI_EXPECT_TRUE(BgraChannel(center, 8) < 40);
    XER_UI_EXPECT_TRUE(BgraChannel(center, 0) < 40);
    // Outside the rect: clear color (black).
    const std::uint32_t corner = px[2 * 64 + 2];
    XER_UI_EXPECT_TRUE(BgraChannel(corner, 16) < 20);
}

XER_UI_TEST(SoftwareClipAndLayerAffectFill) {
    SoftwareRenderer renderer(nullptr);
    renderer.SetDpi(96);
    renderer.SetClearColor({0.0f, 0.0f, 0.0f, 1.0f});
    renderer.BeginFrame(Rect{0.0f, 0.0f, 32.0f, 32.0f});
    renderer.PushClip(Rect{0.0f, 0.0f, 16.0f, 16.0f});
    renderer.FillRect(Rect{0.0f, 0.0f, 32.0f, 32.0f},
                      {0.0f, 1.0f, 0.0f, 1.0f}, 0.0f);
    renderer.PopClip();
    renderer.PushLayer(0.5f, 0, 0.0f);
    renderer.FillRect(Rect{24.0f, 24.0f, 8.0f, 8.0f},
                      {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f);
    renderer.PopLayer();
    renderer.EndFrame();

    const std::uint32_t* px = renderer.Pixels();
    XER_UI_EXPECT_TRUE(px != nullptr);
    if (px == nullptr) {
        return;
    }
    // Inside clip: green painted.
    XER_UI_EXPECT_TRUE(BgraChannel(px[8 * 32 + 8], 8) > 200);
    // Outside clip: untouched black.
    XER_UI_EXPECT_TRUE(BgraChannel(px[8 * 32 + 24], 8) < 40);
    // Half-opacity white over black -> mid grey (~127).
    const std::uint32_t grey = px[28 * 32 + 28];
    XER_UI_EXPECT_TRUE(BgraChannel(grey, 16) > 100 &&
                       BgraChannel(grey, 16) < 160);
}

XER_UI_TEST(SoftwareMeasureTextIsPositive) {
    SoftwareRenderer renderer(nullptr);
    const Size s = renderer.MeasureText("hello", TextStyle{});
    XER_UI_EXPECT_TRUE(s.width > 0.0f);
    XER_UI_EXPECT_TRUE(s.height > 0.0f);
}
