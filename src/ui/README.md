# XER UI Module

Universal WinAPI-based UI framework with ShadcnUI-style widgets, full ColorSpace support, and HDR rendering.

## Overview

A deep module providing a modern UI framework built on pure WinAPI with:
- **Small Interface**: Minimal public API with maximum hidden complexity
- **High Leverage**: Complex UI operations through simple function calls
- **ColorSpace Support**: Full support for sRGB, Display P3, Rec.2020, and HDR (PQ/HLG)
- **HDR Rendering**: Windows HDR API integration for high dynamic range displays
- **ShadcnUI Style**: Modern, clean widget design inspired by ShadcnUI
- **Deep Architecture**: All WinAPI complexity hidden in platform layer

## Architecture

```
src/ui/
├── core/                  # Core framework (interface layer)
│   ├── application.h/cc   # Application lifecycle
│   └── window.h/cc         # Window management
├── colorspace/            # ColorSpace & HDR support
│   ├── color_space.h      # Color space enums and utilities
│   ├── hdr_manager.h/cc    # HDR display management
│   └── color_manager.h/cc  # Unified color management
├── widgets/               # Widget system
│   └── widget.h           # Base widget interface
├── layout/                # Layout system
│   └── geometry.h         # Geometric primitives (Rect, Size, Point)
├── theme/                 # ShadcnUI-style theming (TODO)
├── rendering/             # Rendering abstraction (TODO)
└── platform/              # WinAPI platform layer (TODO)
```

## Design Principles

### Deep Module Strategy
- **Small Interface**: Minimal public methods (e.g., Application has only 3 public methods)
- **Deep Implementation**: Complex logic hidden in private implementation (PIMPL pattern)
- **High Leverage**: One interface call enables complex operations
- **Local Changes**: Modifications contained within module, don't affect callers

### Seam Placement
- **External Seam**: Application, Window, Widget interfaces
- **Internal Seams**: Renderer adapters, layout strategies, theme implementations
- **Platform Seam**: WinAPI-specific code isolated in platform/ directory

## Quick Start

### Basic Window

```cpp
#include "ui/core/application.h"
#include "ui/core/window.h"

using namespace XER::UI::Core;

int main() {
    // Initialize application
    auto& app = Application::Instance();
    if (!app.Initialize()) {
        return -1;
    }

    // Create window
    Window window;
    window.Create("My Window", 800, 600);
    window.Show();

    // Run event loop
    return app.Run();
}
```

### ColorSpace and HDR

```cpp
#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/colorspace/color_manager.h"

using namespace XER::UI;
using namespace XER::UI::ColorSpace;

int main() {
    auto& app = Application::Instance();
    app.Initialize();

    // Initialize color management
    auto& color_mgr = ColorManager::Instance();
    color_mgr.Initialize();

    // Check HDR capability
    if (color_mgr.IsHDRCapable()) {
        std::cout << "HDR is supported!" << std::endl;

        // Set preferred color space
        color_mgr.SetPreferredColorSpace(ColorSpace::Rec2100_PQ);

        // Get HDR metadata
        auto& hdr_mgr = color_mgr.GetHDRManager();
        HDRMetadata metadata = hdr_mgr.GetHDRMetadata();
        std::cout << "Max luminance: " << metadata.max_luminance << " nits" << std::endl;
    }

    Window window;
    window.Create("HDR Window", 800, 600);

    // Enable HDR for the window
    if (color_mgr.IsHDRCapable()) {
        color_mgr.GetHDRManager().EnableHDR(window.GetHWND());
    }

    window.Show();
    return app.Run();
}
```

### Color Conversion

```cpp
#include "ui/colorspace/color_manager.h"

using namespace XER::UI::ColorSpace;

// Convert colors between color spaces
ColorManager& color_mgr = ColorManager::Instance();
color_mgr.Initialize();

std::vector<float> input_rgb(pixel_count * 3);  // sRGB colors
std::vector<float> output_rgb(pixel_count * 3);  // Output buffer

// Convert sRGB to Rec.2020
color_mgr.ConvertToDisplaySpace(
    input_rgb.data(),
    output_rgb.data(),
    pixel_count,
    ColorSpace::sRGB,
    ColorSpace::Rec2020
);
```

## Core Modules

### Application

Main entry point with small interface:

```cpp
class Application {
public:
    static Application& Instance();
    bool Initialize();  // Setup platform
    int Run();          // Start event loop
    void Shutdown();    // Cleanup
};
```

**Deep Implementation Hides**:
- WinAPI initialization
- Common controls registration
- Message pump
- Process priority management

### Window

Window management with small interface:

```cpp
class Window {
public:
    bool Create(const std::string& title, int width, int height);
    void Show();
    void Hide();
    void Close();
    void SetContent(std::shared_ptr<Widget> root);
    HWND GetHWND() const;  // Platform access when needed
};
```

**Deep Implementation Hides**:
- Window class registration
- WinAPI window creation
- Message handling
- Widget tree management

### ColorSpace

Color space enumeration and utilities:

```cpp
enum class ColorSpace {
    sRGB,          // Standard sRGB
    DisplayP3,     // Wide gamut
    Rec2020,       // Ultra-wide gamut
    Rec2100_PQ,    // HDR PQ
    Rec2100_HLG,   // HDR HLG
    ScRGB          // HDR linear
};

enum class TransferFunction {
    sRGB, PQ, HLG, Linear, Gamma22, Gamma18
};
```

### HDRManager

HDR display management:

```cpp
class HDRManager {
public:
    bool Initialize();
    bool IsHDRCapable() const;
    HDRMetadata GetHDRMetadata() const;
    bool EnableHDR(HWND hwnd);
    bool DisableHDR(HWND hwnd);
};
```

**Deep Implementation Hides**:
- Windows HDR API (IDXGIOutput)
- Display capability detection
- HDR metadata configuration
- Dolby Vision detection

### ColorManager

Unified color management:

```cpp
class ColorManager {
public:
    void SetPreferredColorSpace(ColorSpace space);
    ColorSpace GetSystemColorSpace() const;
    void ConvertToDisplaySpace(...);
    void ApplyTransferFunction(...);
    HDRManager& GetHDRManager();
};
```

**Deep Implementation Hides**:
- Color space conversion matrices
- Transfer function application
- System color space detection
- Integration with HDRManager

## Supported Color Spaces

| Color Space | Gamut | HDR | Transfer | Use Case |
|-------------|-------|-----|----------|----------|
| sRGB | Standard | No | sRGB gamma | Standard UI |
| Display P3 | Wide | No | sRGB gamma | Apple displays |
| Rec.2020 | Ultra-wide | No | sRGB gamma | High-end displays |
| Rec.2100 PQ | Ultra-wide | Yes | PQ | HDR video |
| Rec.2100 HLG | Ultra-wide | Yes | HLG | HDR broadcasting |
| ScRGB | Ultra-wide | Yes | Linear | HDR graphics |

## HDR Support

### Detection
Automatic detection of HDR capabilities:
- Windows version check (Windows 10 1803+)
- DXGI display capability query
- EDID parsing for HDR support
- Dolby Vision detection

### Configuration
HDR parameters can be configured:
- Max luminance (nits)
- Min luminance (black level)
- Max frame average light level

### Window-level HDR
HDR can be enabled per-window:
```cpp
color_mgr.GetHDRManager().EnableHDR(window.GetHWND());
```

## Implementation Status

### Completed
- ✅ Core framework (Application, Window)
- ✅ ColorSpace enumeration and utilities
- ✅ HDRManager (basic implementation)
- ✅ ColorManager with color conversion
- ✅ Base Widget interface
- ✅ Geometry primitives (Rect, Size, Point)

### TODO
- ⏳ Rendering abstraction (Direct2D/GDI+)
- ⏳ Widget implementations (Button, Text, Container)
- ⏳ Layout system (Flex, Grid)
- ⏳ ShadcnUI-style theming
- ⏳ Platform layer (WinAPI details)
- ⏳ Direct2D renderer with HDR support
- ⏳ Comprehensive tests
- ⏳ Example applications

## Building

### Requirements
- Windows 10 or later
- Visual Studio 2019 or later
- C++23 support
- Windows SDK
- DirectX SDK (for Direct2D)

### CMake Integration
Add to CMakeLists.txt:

```cmake
# UI module
if(WIN32)
    file(GLOB_RECURSE UI_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/**/*.cc"
    )
    file(GLOB_RECURSE UI_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/**/*.h"
    )

    add_library(XER_UI STATIC ${UI_SOURCES} ${UI_HEADERS})
    target_link_libraries(XER_UI
        PRIVATE
            user32
            gdi32
            d2d1
            dxgi
    )
endif()
```

## Deep Module Benefits

### Leverage
- `Application::Run()` handles entire UI lifecycle
- Single `Window::Create()` handles window registration, creation, and setup
- `ColorManager::ConvertToDisplaySpace()` handles complex color space conversions

### Locality
- All WinAPI complexity in platform/ directory
- Color conversion logic in colorspace/ directory
- Widget rendering in widgets/ directory
- Changes don't spread across codebase

### Testability
- Renderer interface mockable for testing
- Widget logic testable without WinAPI
- Color conversion testable independently
- Clean seams for dependency injection

## Examples

See `examples/` directory for:
- Basic window example
- HDR window example
- Color conversion example
- Widget example (when implemented)

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - Detailed architecture documentation
- [README.md](README.md) - This file
- Inline documentation in headers

## Contributing

When adding new features:
1. Keep the interface small
2. Hide complexity in implementation
3. Use PIMPL pattern for ABI stability
4. Document the interface clearly
5. Add tests for the interface
6. Update this README

## License

Part of XER project. See main project LICENSE.
