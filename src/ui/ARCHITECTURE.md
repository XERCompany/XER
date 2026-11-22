# XER UI Module Architecture

## Overview

A deep module providing universal WinAPI-based UI with ShadcnUI-style widgets, full ColorSpace support, and HDR rendering.

## Design Principles

### Deep Module Strategy
- **Small Interface**: Minimal public API with maximum hidden complexity
- **High Leverage**: One interface call enables complex UI operations
- **Locality**: All UI complexity contained within the module
- **Testability**: Clean seam for mocking and testing

### Module Structure

```
src/ui/
├── core/                  # Core framework (interface layer)
│   ├── application.h/cc
│   ├── window.h/cc
│   ├── event_loop.h/cc
│   └── message_handler.h/cc
├── rendering/             # Rendering abstraction
│   ├── renderer.h/cc
│   ├── direct2d_renderer.h/cc
│   ├── gdi_renderer.h/cc
│   └── graphics_context.h/cc
├── colorspace/            # ColorSpace & HDR support
│   ├── color_space.h/cc
│   ├── hdr_manager.h/cc
│   ├── color_converter.h/cc
│   └── sc_gamut.h/cc
├── widgets/               # Widget system
│   ├── widget.h/cc
│   ├── button.h/cc
│   ├── text.h/cc
│   ├── container.h/cc
│   ├── scroll_view.h/cc
│   └── widget_tree.h/cc
├── layout/                # Layout system
│   ├── layout.h/cc
│   ├── flex_layout.h/cc
│   ├── grid_layout.h/cc
│   └── constraints.h/cc
├── theme/                 # ShadcnUI-style theming
│   ├── theme.h/cc
│   ├── color_palette.h/cc
│   ├── typography.h/cc
│   └── tokens.h/cc
├── platform/              # WinAPI platform layer
│   ├── win32_window.h/cc
│   ├── win32_graphics.h/cc
│   └── win32_events.h/cc
└── README.md
```

## Core Interface

### Application Module
```cpp
namespace XER::UI {

class Application {
public:
    // Small interface - just 3 public methods
    static Application& Instance();
    bool Initialize();
    int Run();
    void Shutdown();

private:
    // Deep implementation hidden
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI
```

### Window Module
```cpp
namespace XER::UI {

class Window {
public:
    // Small interface
    using Callback = std::function<void()>;

    Window();
    ~Window();

    bool Create(const std::string& title, int width, int height);
    void Show();
    void Hide();
    void Close();

    void SetContent(std::shared_ptr<Widget> root);
    void SetOnClose(Callback callback);

    HWND GetHWND() const; // Platform access when needed

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI
```

## ColorSpace & HDR Module

### Interface
```cpp
namespace XER::UI::ColorSpace {

enum class ColorSpace {
    sRGB,
    DisplayP3,
    Rec2020,
    Rec2100_PQ,  // HDR PQ
    Rec2100_HLG  // HDR HLG
};

enum class TransferFunction {
    sRGB,
    PQ,    // Perceptual Quantizer (HDR)
    HLG,   // Hybrid Log-Gamma (HDR)
    Linear
};

class ColorManager {
public:
    // Small interface
    static ColorManager& Instance();

    bool Initialize();
    void SetPreferredColorSpace(ColorSpace space);
    ColorSpace GetSystemColorSpace() const;
    bool IsHDRCapable() const;

    // Conversion interface
    void ConvertToDisplaySpace(
        const float* input,
        float* output,
        int pixel_count,
        ColorSpace source,
        ColorSpace target
    );

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::ColorSpace
```

## Widget System

### Base Widget Interface
```cpp
namespace XER::UI::Widgets {

class Widget {
public:
    virtual ~Widget() = default;

    // Small interface - core operations only
    virtual void Render(Renderer& renderer) = 0;
    virtual void Layout(const Rect& bounds) = 0;
    virtual bool HandleEvent(const Event& event) = 0;

    // Size hints (+ Qt aliases sizeHint() / minimumSizeHint())
    virtual Size GetMinSize() const = 0;
    virtual Size GetPreferredSize() const = 0;
    virtual Size GetMaxSize() const = 0;

    // Assigned slot ↔ adopted bounds
    Rect GetBounds() const;
    void SetBounds(const Rect& bounds);   // resolves the box model

    // Universal layout-geometry contract (Qt/views model). Uniform
    // properties, honored centrally in SetBounds — every container
    // respects them for every laid-out child. All defaults are no-ops.
    void SetAlignment(Alignment);   // Qt::Alignment-style flags
    void SetMargin(Layout::Insets); // box-model outer spacing
    void SetIndent(int);            // direction-aware leading inset
    void SetScaledContents(bool);   // QLabel-style content stretch
    void SetFixedSize(Size)/SetFixedWidth/SetFixedHeight; // Qt sizing
    void Move(...)/Resize(...);     // explicit geometry
    rect()/pos()/size()/x()/y()/width()/height();         // Qt aliases

protected:
    Widget() = default;
};

} // namespace XER::UI::Widgets
```

The alignment type is a flags enum (`Widgets::Alignment`: Leading/Left/
HCenter/Right/Trailing/HFill combined with Top/VCenter/Bottom/VFill via
`operator|`). Resolution order in `SetBounds`: slot → minus margin +
leading-edge indent → fixed extents clamp preferred → aligned position.
`Alignment::Default` fills the slot — the legacy contract.

`widgets/surface.h` provides `Surface`, the generic custom-paint widget
(`SetPaintCallback`/`SetEventCallback`) for app-hosted rendering; it
honors `hasScaledContents()` when letterboxing a `ContentSize`.

### Button Widget
```cpp
namespace XER::UI::Widgets {

class Button : public Widget {
public:
    using ClickCallback = std::function<void()>;

    static std::shared_ptr<Button> Create(
        const std::string& text,
        ClickCallback callback = nullptr
    );

    void SetText(const std::string& text);
    void SetEnabled(bool enabled);
    void SetStyle(const ButtonStyle& style);

    // Widget interface implementation
    void Render(Renderer& renderer) override;
    void Layout(const Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Size GetMinSize() const override;
    Size GetPreferredSize() const override;
    Size GetMaxSize() const override;

private:
    Button(const std::string& text, ClickCallback callback);
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Widgets
```

## Theme System (ShadcnUI-style)

### Theme Interface
```cpp
namespace XER::UI::Theme {

struct ColorToken {
    std::string name;
    float light[3];  // RGB 0-1
    float dark[3];   // RGB 0-1
};

struct TypographyToken {
    std::string name;
    std::string font_family;
    float font_size;
    float line_height;
    float letter_spacing;
};

class Theme {
public:
    static Theme& Instance();

    void SetColorMode(bool dark);
    bool IsDarkMode() const;

    // Color access
    ColorToken GetColor(const std::string& name) const;
    ColorToken GetAccentColor() const;

    // Typography access
    TypographyToken GetTypography(const std::string& name) const;

    // Predefined ShadcnUI-style palettes
    void LoadZincPalette();
    void LoadSlatePalette();
    void LoadNeutralPalette();

private:
    Theme() = default;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Theme
```

## Layout System

### Layout Interface
```cpp
namespace XER::UI::Layout {

class Layout {
public:
    virtual ~Layout() = default;

    // Small interface
    virtual void Arrange(
        const std::vector<std::shared_ptr<Widget>>& children,
        const Rect& bounds
    ) = 0;

    virtual Size Measure(
        const std::vector<std::shared_ptr<Widget>>& children,
        const Size& available
    ) = 0;
};

class FlexLayout : public Layout {
public:
    static std::shared_ptr<FlexLayout> Create();

    void SetDirection(FlexDirection direction);
    void SetJustifyContent(JustifyContent justify);
    void SetAlignItems(AlignItems align);
    void SetGap(float gap);

    // Layout interface
    void Arrange(...) override;
    Size Measure(...) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Layout
```

## Rendering Abstraction

### Renderer Interface
```cpp
namespace XER::UI::Rendering {

class Renderer {
public:
    virtual ~Renderer() = default;

    // Small interface - core drawing operations
    virtual void Clear(const Color& color) = 0;
    virtual void DrawRect(const Rect& rect, const Brush& brush) = 0;
    virtual void DrawText(const TextRun& text, const Point& position) = 0;
    virtual void DrawImage(const Image& image, const Rect& dest) = 0;

    // State
    virtual void PushTransform(const Matrix& transform) = 0;
    virtual void PopTransform() = 0;
    virtual void SetClip(const Rect& rect) = 0;

    // HDR support
    virtual void SetColorSpace(ColorSpace space) = 0;
    virtual void SetHDRMetadata(const HDRMetadata& metadata) = 0;

protected:
    Renderer() = default;
};

class Direct2DRenderer : public Renderer {
public:
    static std::unique_ptr<Direct2DRenderer> Create(HWND hwnd);

    // Renderer interface implementation
    void Clear(const Color& color) override;
    void DrawRect(const Rect& rect, const Brush& brush) override;
    // ... etc

private:
    Direct2DRenderer() = default;
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Rendering
```

### Renderer Providers (backends)

`rendering/renderer_provider.h` defines `RendererBackend`
(`kAuto | kSoftware | kD2D | kD3D11 | kVulkan`), a `RendererProvider`
contract, and the factory:

```cpp
std::unique_ptr<Renderer> CreateRenderer(RendererBackend, void* hwnd);
std::unique_ptr<Renderer> CreateRendererFromEnv(void* hwnd);
```

Selection precedence: `XER_UI_RENDERER=vulkan|d2d|d3d11|software|auto`
(case-insensitive) > the automatic chain. The factory always walks the
fallback chain (`kD2D`/`kD3D11` -> `kVulkan` -> `kSoftware`) so a request
for a missing backend (e.g. no Vulkan ICD) degrades instead of failing.
`kD3D11` is a documented alias: the Direct2D renderer already owns the
D3D11 device + swapchain, so it *is* the D3D11-device provider rather
than a second GPU pipeline.

Backend implementation matrix:

| Op                    | kD2D / kD3D11        | kVulkan (WSI)           | kSoftware (GDI/DIB)      |
|-----------------------|----------------------|-------------------------|--------------------------|
| FillRect (rounded)    | full                 | sharp corners only      | full + AA                |
| StrokeRect / DrawLine | full                 | quad emission           | quad scanline            |
| DrawText / Measure    | DirectWrite          | GDI mask -> texture     | GDI mask -> blend        |
| DrawImage (file)      | WIC -> D2D bitmap    | WIC -> VkImage          | WIC -> blend             |
| BGRA frames/resources | D2D staging bitmaps  | staging -> VkImage      | sampled blend            |
| PushClip / PopClip    | axis-aligned clip    | dynamic scissor (AABB)  | clip-rect stack          |
| PushLayer / blend     | D2D layer (opacity)  | opacity multiplier      | opacity multiplier       |
| SetTransform (2x3)    | DC transform         | folded into NDC quads   | folded into px quads     |
| BackdropBlur          | scrim/shadow approx  | scrim approx            | real box blur            |

Deferred on Vulkan: rounded `FillRect` corners, `corner_radius` image
clipping, blend modes/layer blur beyond opacity (same approximation as
D2D), real frosted-glass blur. The Vulkan renderer is immediate-mode
(`TakeTargetsRecreated()` is always true) so `WidgetTree` repaints the
full frame; the retained D2D canvas keeps partial-damage rendering.

Vulkan needs no SDK to build: headers resolve from the Vulkan SDK,
or the copy vendored in skia's `third_party/vulkan`, and `vulkan-1.dll`
is loaded dynamically. No loader/ICD -> `Create()` returns nullptr ->
factory falls back. `src/config.h`'s `XER_HAVE_VULKAN_VULKAN_H` only
records SDK presence; it does not gate the backend.

## Usage Example

```cpp
#include "ui/application.h"
#include "ui/window.h"
#include "ui/widgets/button.h"
#include "ui/layout/flex_layout.h"
#include "ui/colorspace/color_manager.h"

using namespace XER::UI;

int main() {
    // Initialize application
    auto& app = Application::Instance();
    if (!app.Initialize()) {
        return -1;
    }

    // Setup ColorSpace/HDR
    auto& color_mgr = ColorSpace::ColorManager::Instance();
    color_mgr.Initialize();
    if (color_mgr.IsHDRCapable()) {
        color_mgr.SetPreferredColorSpace(ColorSpace::Rec2100_PQ);
    }

    // Create window
    Window window;
    window.Create("XER UI Demo", 800, 600);

    // Create UI
    auto button = Widgets::Button::Create("Click Me", []() {
        // Handle click
    });

    auto layout = Layout::FlexLayout::Create();
    layout->SetDirection(Layout::FlexDirection::Column);
    layout->SetGap(10.0f);

    window.SetContent(button);
    window.Show();

    // Run event loop
    return app.Run();
}
```

## Implementation Details

### Platform Layer (WinAPI)
- Direct WinAPI calls isolated in `platform/` directory
- Window creation, message pump, event handling
- Direct2D/GDI+ initialization and management
- HDR display detection and configuration

### Deep Module Benefits
1. **Leverage**: Single `Application::Run()` handles entire UI lifecycle
2. **Locality**: All WinAPI complexity hidden in platform layer
3. **Testability**: Renderer and widget interfaces mockable
4. **Maintainability**: Changes to WinAPI don't affect widget code

### Seam Placement
- **External seam**: `Application`, `Window`, `Widget` interfaces
- **Internal seams**: Renderer adapters, layout strategies, theme implementations
- **Platform seam**: WinAPI-specific code in `platform/` directory

## Next Steps

1. Implement core framework (Application, Window, EventLoop)
2. Create ColorSpace module with HDR support
3. Implement base widget system
4. Add Direct2D renderer with HDR
5. Create ShadcnUI-style theme system
6. Implement layout algorithms
7. Add example applications
8. Write comprehensive tests
