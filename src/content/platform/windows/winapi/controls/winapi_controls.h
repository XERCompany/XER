#pragma once

#include "../winapi_common.h"
#include <functional>
#include <map>

namespace Engine::Content::Platform::WinAPI::Controls {

// Window creation parameters
struct WindowParams {
    std::string className;
    std::string windowTitle;
    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = 0;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = CW_USEDEFAULT;
    int height = CW_USEDEFAULT;
    HWND parent = NULL;
    HMENU menu = NULL;
    LPVOID param = NULL;
};

// Window wrapper
class Window {
public:
    using WndProcCallback = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)>;
    
    Window();
    ~Window();
    
    bool Create(const WindowParams& params, WNDPROC wndProc = nullptr);
    bool Create(const WindowParams& params, WndProcCallback callback);
    
    void Destroy();
    void Show(int nCmdShow = SW_SHOW);
    void Hide();
    void Update();
    void Invalidate();
    
    HWND GetHandle() const { return hwnd_; }
    bool IsValid() const { return hwnd_ != NULL; }
    
    void SetText(const std::string& text);
    std::string GetText() const;
    
    void SetPosition(int x, int y, int width, int height, BOOL repaint = TRUE);
    void GetPosition(RECT* rect) const;
    
    void Enable(bool enable = true);
    bool IsEnabled() const;
    
    void SetFocus();
    bool HasFocus() const;
    
private:
    HWND hwnd_;
    WndProcCallback callback_;
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static std::map<HWND, Window*> windowMap_;
};

// Button control
class Button {
public:
    Button();
    ~Button();
    
    bool Create(HWND parent, int id, const std::string& text, 
                int x, int y, int width, int height);
    void Destroy();
    
    HWND GetHandle() const { return hwnd_; }
    void SetText(const std::string& text);
    std::string GetText() const;
    void Enable(bool enable = true);
    
private:
    HWND hwnd_;
};

// Edit control (text input)
class EditControl {
public:
    EditControl();
    ~EditControl();
    
    bool Create(HWND parent, int id, const std::string& text,
                int x, int y, int width, int height,
                DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL);
    void Destroy();
    
    HWND GetHandle() const { return hwnd_; }
    void SetText(const std::string& text);
    std::string GetText() const;
    void SetReadOnly(bool readOnly = true);
    void SetPasswordChar(char ch = '*');
    
private:
    HWND hwnd_;
};

// Static control (label)
class StaticControl {
public:
    StaticControl();
    ~StaticControl();
    
    bool Create(HWND parent, int id, const std::string& text,
                int x, int y, int width, int height);
    void Destroy();
    
    HWND GetHandle() const { return hwnd_; }
    void SetText(const std::string& text);
    std::string GetText() const;
    
private:
    HWND hwnd_;
};

// Listbox control
class ListBox {
public:
    ListBox();
    ~ListBox();
    
    bool Create(HWND parent, int id, 
                int x, int y, int width, int height);
    void Destroy();
    
    HWND GetHandle() const { return hwnd_; }
    
    int AddString(const std::string& text);
    int InsertString(int index, const std::string& text);
    int DeleteString(int index);
    void ResetContent();
    
    int GetCount() const;
    int GetCurSel() const;
    void SetCurSel(int index);
    std::string GetText(int index) const;
    
private:
    HWND hwnd_;
};

// ComboBox control
class ComboBox {
public:
    ComboBox();
    ~ComboBox();
    
    bool Create(HWND parent, int id,
                int x, int y, int width, int height,
                DWORD style = CBS_DROPDOWN | WS_CHILD | WS_VISIBLE);
    void Destroy();
    
    HWND GetHandle() const { return hwnd_; }
    
    int AddString(const std::string& text);
    int InsertString(int index, const std::string& text);
    int DeleteString(int index);
    void ResetContent();
    
    int GetCount() const;
    int GetCurSel() const;
    void SetCurSel(int index);
    std::string GetText(int index) const;
    std::string GetEditText() const;
    void SetEditText(const std::string& text);
    
private:
    HWND hwnd_;
};

}  // namespace Engine::Content::Platform::WinAPI::Controls
