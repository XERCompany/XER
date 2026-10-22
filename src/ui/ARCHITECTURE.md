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

    // Size hints
    virtual Size GetMinSize() const = 0;
    virtual Size GetPreferredSize() const = 0;
    virtual Size GetMaxSize() const = 0;

protected:
    Widget() = default;
};

} // namespace XER::UI::Widgets
```

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
