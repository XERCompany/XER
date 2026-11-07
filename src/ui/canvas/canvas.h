#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// windows.h (via transitive includes) may #define DrawText -> DrawTextA/W.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::UI::Canvas {

// Immediate-mode 2D canvas backed by a child HWND with a Skia raster
// surface (vcpkg unofficial-skia). All public coordinates are in DIPs —
// the same space the widget layout uses; the surface itself is physical
// pixels and the Skia canvas is scaled by the window DPI during paint.
//
// Usage:
//   Canvas canvas;
//   canvas.Create(parent_hwnd);
//   canvas.SetBounds(x, y, w, h);
//   canvas.SetRenderCallback([](Canvas& c) { c.Clear({0,0,0,1}); c.FillRect(...); });
//   canvas.Invalidate();  // schedules WM_PAINT -> callback
class Canvas {
public:
    struct Color {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    };
    struct Rect {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    };

    Canvas();
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool Create(void* parent_hwnd);
    void Destroy();
    bool IsCreated() const { return hwnd_ != nullptr; }

    // Child-window placement in parent client coordinates.
    void SetBounds(float x, float y, float w, float h);
    void GetBounds(float& x, float& y, float& w, float& h) const;

    void SetRenderCallback(std::function<void(Canvas&)> callback);
    // Mouse/keyboard input forwarded from the child HWND (client coords in
    // DIPs). button: 0=left 1=right 2=middle (-1=none); wheel_delta in
    // WHEEL_DELTA units; key is the Win32 VK/WM_CHAR code for key messages;
    // mods is a bitmask of MK_* / modifier state at event time.
    using InputCallback = std::function<bool(unsigned msg, int x, int y,
                                              int button, int wheel_delta,
                                              int key, int mods)>;
    void SetInputCallback(InputCallback callback);
    void SetVisible(bool visible);
    void Invalidate();
    // Moves the canvas HWND to the bottom of the sibling z-order so other
    // child windows (plugin editors, previews) always stay on top.
    void SetZOrderBottom();

    // --- Drawing API (valid inside the render callback) ---
    void Clear(Color color);
    void FillRect(Rect rect, Color color, float radius = 0.0f);
    void StrokeRect(Rect rect, Color color, float width = 1.0f, float radius = 0.0f);
    void DrawLine(float x1, float y1, float x2, float y2, Color color, float width = 1.0f);
    // Cubic bezier segment (node-graph edges). dash_on > 0 draws a dashed
    // path with that on/off segment length (in canvas units).
    void DrawCubicBezier(float x1, float y1, float cx1, float cy1,
                         float cx2, float cy2, float x2, float y2,
                         Color color, float width = 1.0f,
                         float dash_on = 0.0f, float dash_off = 0.0f);
    void FillCircle(float cx, float cy, float radius, Color color);
    // Simple filled text at baseline origin (Skia default font).
    void DrawText(std::string_view text, float x, float y,
                  float size_px, Color color);
    // Scissor clip in canvas (DIP) coordinates; Push/Pop nest like a stack.
    // Drawing outside the top clip is discarded. Balanced use is required —
    // extra pops are ignored, unclosed pushes are reset each frame.
    void PushClip(Rect rect);
    void PopClip();
    // Uploads BGRA pixels and draws them stretched over the whole canvas.
    // Safe to call inside the callback; data is copied, may be nullptr-sized.
    void DrawImageBgra(int width, int height, const void* bgra_pixels);

    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

    // Internal: window-procedure trampoline registered with the canvas class.
    static intptr_t WndProcTrampoline(void* hwnd, unsigned int msg, intptr_t wparam, intptr_t lparam);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void* hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::function<void(Canvas&)> render_callback_;
    InputCallback input_callback_;
};

} // namespace XER::UI::Canvas
