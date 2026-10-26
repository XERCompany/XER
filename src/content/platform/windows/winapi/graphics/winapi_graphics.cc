#include "winapi_graphics.h"

namespace Engine::Content::Platform::WinAPI::Graphics {

// ---------------------------------------------------------------------------
// GDI Helper
// ---------------------------------------------------------------------------

HPEN GDIHelper::CreatePen(int style, int width, COLORREF color) {
    return ::CreatePen(style, width, color);
}

HBRUSH GDIHelper::CreateSolidBrush(COLORREF color) {
    return ::CreateSolidBrush(color);
}

HBRUSH GDIHelper::CreateHatchBrush(int hatchStyle, COLORREF color) {
    return ::CreateHatchBrush(hatchStyle, color);
}

HFONT GDIHelper::CreateFont(const std::string& faceName, int height,
                           int weight, BOOL italic) {
    return ::CreateFontA(
        height,                     // height
        0,                          // width (0 = default)
        0,                          // escapement
        0,                          // orientation
        weight,                     // weight
        italic,                     // italic
        FALSE,                      // underline
        FALSE,                      // strikeout
        DEFAULT_CHARSET,            // charset
        OUT_DEFAULT_PRECIS,         // output precision
        CLIP_DEFAULT_PRECIS,        // clipping precision
        DEFAULT_QUALITY,            // quality
        DEFAULT_PITCH | FF_DONTCARE,// pitch and family
        faceName.c_str()            // face name
    );
}

void GDIHelper::DeleteObject(HGDIOBJ obj) {
    if (obj) {
        ::DeleteObject(obj);
    }
}

HDC GDIHelper::GetWindowDC(HWND hwnd) {
    return ::GetWindowDC(hwnd);
}

void GDIHelper::ReleaseDC(HWND hwnd, HDC hdc) {
    if (hdc) {
        ::ReleaseDC(hwnd, hdc);
    }
}

// ---------------------------------------------------------------------------
// Device Context
// ---------------------------------------------------------------------------

DeviceContext::DeviceContext(HWND hwnd) 
    : hdc_(GetDC(hwnd)), owned_(true) {
}

DeviceContext::DeviceContext(HDC hdc, bool owned)
    : hdc_(hdc), owned_(owned) {
}

DeviceContext::~DeviceContext() {
    if (hdc_ && owned_) {
        ReleaseDC(NULL, hdc_);
    }
}

void DeviceContext::SetTextColor(COLORREF color) {
    if (hdc_) {
        ::SetTextColor(hdc_, color);
    }
}

void DeviceContext::SetBkColor(COLORREF color) {
    if (hdc_) {
        ::SetBkColor(hdc_, color);
    }
}

void DeviceContext::SetBkMode(int mode) {
    if (hdc_) {
        ::SetBkMode(hdc_, mode);
    }
}

void DeviceContext::SelectObject(HGDIOBJ obj) {
    if (hdc_) {
        ::SelectObject(hdc_, obj);
    }
}

void DeviceContext::Rectangle(int left, int top, int right, int bottom) {
    if (hdc_) {
        ::Rectangle(hdc_, left, top, right, bottom);
    }
}

void DeviceContext::Ellipse(int left, int top, int right, int bottom) {
    if (hdc_) {
        ::Ellipse(hdc_, left, top, right, bottom);
    }
}

void DeviceContext::Line(int x1, int y1, int x2, int y2) {
    if (hdc_) {
        ::MoveToEx(hdc_, x1, y1, NULL);
        ::LineTo(hdc_, x2, y2);
    }
}

void DeviceContext::Text(int x, int y, const std::string& text) {
    if (hdc_) {
        ::TextOutA(hdc_, x, y, text.c_str(), text.length());
    }
}

SIZE DeviceContext::GetTextExtent(const std::string& text) {
    SIZE size = {0, 0};
    if (hdc_) {
        ::GetTextExtentPoint32A(hdc_, text.c_str(), text.length(), &size);
    }
    return size;
}

// ---------------------------------------------------------------------------
// Direct2D Factory
// ---------------------------------------------------------------------------

Direct2DFactory::Direct2DFactory() 
    : d2dFactory_(nullptr), initialized_(false) {
}

Direct2DFactory::~Direct2DFactory() {
    Shutdown();
}

Direct2DFactory& Direct2DFactory::Instance() {
    static Direct2DFactory instance;
    return instance;
}

bool Direct2DFactory::Initialize() {
    if (initialized_) return true;
    
    // Note: This is a placeholder for Direct2D initialization
    // Full implementation would load d2d1.dll and create ID2D1Factory
    // For now, we'll return false as it requires additional setup
    
    // TODO: Implement actual Direct2D initialization
    // HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
    // initialized_ = SUCCEEDED(hr);
    
    initialized_ = false;
    return initialized_;
}

void Direct2DFactory::Shutdown() {
    if (d2dFactory_) {
        // Release COM object
        // static_cast<ID2D1Factory*>(d2dFactory_)->Release();
        d2dFactory_ = nullptr;
    }
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// Display Enumerator
// ---------------------------------------------------------------------------

std::vector<DisplayEnumerator::DisplayInfo> DisplayEnumerator::EnumerateDisplays() {
    std::vector<DisplayInfo> displays;
    
    DISPLAY_DEVICE displayDevice;
    displayDevice.cb = sizeof(DISPLAY_DEVICE);
    
    DWORD deviceIndex = 0;
    while (EnumDisplayDevicesA(NULL, deviceIndex, &displayDevice, 0)) {
        DEVMODE devMode;
        devMode.dmSize = sizeof(DEVMODE);
        
        if (EnumDisplaySettingsA(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS, &devMode)) {
            DisplayInfo info;
            info.name = displayDevice.DeviceName;
            info.x = devMode.dmPosition.x;
            info.y = devMode.dmPosition.y;
            info.width = devMode.dmPelsWidth;
            info.height = devMode.dmPelsHeight;
            info.isPrimary = (displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
            
            displays.push_back(info);
        }
        
        deviceIndex++;
    }
    
    return displays;
}

DisplayEnumerator::DisplayInfo DisplayEnumerator::GetPrimaryDisplay() {
    auto displays = EnumerateDisplays();
    for (const auto& display : displays) {
        if (display.isPrimary) {
            return display;
        }
    }
    
    // Fallback to virtual screen if no primary found
    DisplayInfo info;
    info.name = "Virtual Screen";
    info.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    info.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    info.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    info.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    info.isPrimary = true;
    
    return info;
}

}  // namespace Engine::Content::Platform::WinAPI::Graphics
