#pragma once

#include "../winapi_common.h"
#include <memory>

namespace Engine::Content::Platform::WinAPI::Graphics {

// GDI helper functions
class GDIHelper {
public:
    static HPEN CreatePen(int style, int width, COLORREF color);
    static HBRUSH CreateSolidBrush(COLORREF color);
    static HBRUSH CreateHatchBrush(int hatchStyle, COLORREF color);
    static HFONT CreateFont(const std::string& faceName, int height, 
                           int weight = FW_NORMAL, BOOL italic = FALSE);
    
    static void DeleteObject(HGDIOBJ obj);
    static HDC GetWindowDC(HWND hwnd);
    static void ReleaseDC(HWND hwnd, HDC hdc);
};

// Device context wrapper
class DeviceContext {
public:
    DeviceContext(HWND hwnd);
    DeviceContext(HDC hdc, bool owned = false);
    ~DeviceContext();
    
    HDC Get() const { return hdc_; }
    bool IsValid() const { return hdc_ != NULL; }
    
    // Drawing functions
    void SetTextColor(COLORREF color);
    void SetBkColor(COLORREF color);
    void SetBkMode(int mode);
    
    void SelectObject(HGDIOBJ obj);
    void Rectangle(int left, int top, int right, int bottom);
    void Ellipse(int left, int top, int right, int bottom);
    void Line(int x1, int y1, int x2, int y2);
    void Text(int x, int y, const std::string& text);
    
    SIZE GetTextExtent(const std::string& text);
    
private:
    HDC hdc_;
    bool owned_;
};

// Direct2D initialization (basic wrapper)
class Direct2DFactory {
public:
    static Direct2DFactory& Instance();
    
    bool Initialize();
    void Shutdown();
    
    bool IsAvailable() const { return d2dFactory_ != nullptr; }
    
    // Get D2D factory (ID2D1Factory*)
    void* GetFactory() const { return d2dFactory_; }
    
private:
    Direct2DFactory();
    ~Direct2DFactory();
    
    void* d2dFactory_;  // ID2D1Factory*
    bool initialized_;
};

// DXGI helper for display enumeration
class DisplayEnumerator {
public:
    struct DisplayInfo {
        std::string name;
        int x, y, width, height;
        bool isPrimary;
    };
    
    static std::vector<DisplayInfo> EnumerateDisplays();
    static DisplayInfo GetPrimaryDisplay();
};

}  // namespace Engine::Content::Platform::WinAPI::Graphics
