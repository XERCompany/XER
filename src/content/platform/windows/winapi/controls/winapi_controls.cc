#include "winapi_controls.h"

namespace Engine::Content::Platform::WinAPI::Controls {

// ---------------------------------------------------------------------------
// Window implementation
// ---------------------------------------------------------------------------

std::map<HWND, Window*> Window::windowMap_;

Window::Window() : hwnd_(NULL) {
}

Window::~Window() {
    Destroy();
}

bool Window::Create(const WindowParams& params, WNDPROC wndProc) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    
    hwnd_ = CreateWindowExA(
        params.exStyle,
        params.className.c_str(),
        params.windowTitle.c_str(),
        params.style,
        params.x,
        params.y,
        params.width,
        params.height,
        params.parent,
        params.menu,
        hInstance,
        params.param
    );
    
    return hwnd_ != NULL;
}

bool Window::Create(const WindowParams& params, WndProcCallback callback) {
    callback_ = callback;
    
    // Register a temporary class for callback-based windows
    std::string className = params.className.empty() ? "XERWindow" : params.className;
    
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = className.c_str();
    
    RegisterClassExA(&wc);
    
    WindowParams modifiedParams = params;
    modifiedParams.className = className;
    
    return Create(modifiedParams, StaticWndProc);
}

void Window::Destroy() {
    if (hwnd_) {
        windowMap_.erase(hwnd_);
        DestroyWindow(hwnd_);
        hwnd_ = NULL;
    }
}

void Window::Show(int nCmdShow) {
    if (hwnd_) {
        ShowWindow(hwnd_, nCmdShow);
    }
}

void Window::Hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void Window::Update() {
    if (hwnd_) {
        UpdateWindow(hwnd_);
    }
}

void Window::Invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, NULL, TRUE);
    }
}

void Window::SetText(const std::string& text) {
    if (hwnd_) {
        SetWindowTextA(hwnd_, text.c_str());
    }
}

std::string Window::GetText() const {
    if (!hwnd_) return "";
    
    int len = GetWindowTextLengthA(hwnd_);
    if (len == 0) return "";
    
    std::string text(len + 1, '\0');
    GetWindowTextA(hwnd_, &text[0], len + 1);
    text.resize(len);
    return text;
}

void Window::SetPosition(int x, int y, int width, int height, BOOL repaint) {
    if (hwnd_) {
        MoveWindow(hwnd_, x, y, width, height, repaint);
    }
}

void Window::GetPosition(RECT* rect) const {
    if (hwnd_ && rect) {
        GetWindowRect(hwnd_, rect);
    }
}

void Window::Enable(bool enable) {
    if (hwnd_) {
        EnableWindow(hwnd_, enable ? TRUE : FALSE);
    }
}

bool Window::IsEnabled() const {
    return hwnd_ ? IsWindowEnabled(hwnd_) : false;
}

void Window::SetFocus() {
    if (hwnd_) {
        ::SetFocus(hwnd_);
    }
}

bool Window::HasFocus() const {
    return hwnd_ ? (::GetFocus() == hwnd_) : false;
}

LRESULT CALLBACK Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto it = windowMap_.find(hwnd);
    if (it != windowMap_.end() && it->second->callback_) {
        LRESULT result = it->second->callback_(hwnd, msg, wParam, lParam);
        if (result != 0) return result;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Button implementation
// ---------------------------------------------------------------------------

Button::Button() : hwnd_(NULL) {
}

Button::~Button() {
    Destroy();
}

bool Button::Create(HWND parent, int id, const std::string& text,
                   int x, int y, int width, int height) {
    hwnd_ = CreateWindowExA(
        0,
        "BUTTON",
        text.c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, width, height,
        parent,
        (HMENU)(UINT_PTR)id,
        GetModuleHandle(NULL),
        NULL
    );
    
    return hwnd_ != NULL;
}

void Button::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = NULL;
    }
}

void Button::SetText(const std::string& text) {
    if (hwnd_) {
        SetWindowTextA(hwnd_, text.c_str());
    }
}

std::string Button::GetText() const {
    if (!hwnd_) return "";
    
    int len = GetWindowTextLengthA(hwnd_);
    if (len == 0) return "";
    
    std::string text(len + 1, '\0');
    GetWindowTextA(hwnd_, &text[0], len + 1);
    text.resize(len);
    return text;
}

void Button::Enable(bool enable) {
    if (hwnd_) {
        EnableWindow(hwnd_, enable ? TRUE : FALSE);
    }
}

// ---------------------------------------------------------------------------
// EditControl implementation
// ---------------------------------------------------------------------------

EditControl::EditControl() : hwnd_(NULL) {
}

EditControl::~EditControl() {
    Destroy();
}

bool EditControl::Create(HWND parent, int id, const std::string& text,
                        int x, int y, int width, int height, DWORD style) {
    hwnd_ = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        text.c_str(),
        style,
        x, y, width, height,
        parent,
        (HMENU)(UINT_PTR)id,
        GetModuleHandle(NULL),
        NULL
    );
    
    return hwnd_ != NULL;
}

void EditControl::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = NULL;
    }
}

void EditControl::SetText(const std::string& text) {
    if (hwnd_) {
        SetWindowTextA(hwnd_, text.c_str());
    }
}

std::string EditControl::GetText() const {
    if (!hwnd_) return "";
    
    int len = GetWindowTextLengthA(hwnd_);
    if (len == 0) return "";
    
    std::string text(len + 1, '\0');
    GetWindowTextA(hwnd_, &text[0], len + 1);
    text.resize(len);
    return text;
}

void EditControl::SetReadOnly(bool readOnly) {
    if (hwnd_) {
        SendMessage(hwnd_, EM_SETREADONLY, readOnly ? TRUE : FALSE, 0);
    }
}

void EditControl::SetPasswordChar(char ch) {
    if (hwnd_) {
        SendMessage(hwnd_, EM_SETPASSWORDCHAR, ch, 0);
    }
}

// ---------------------------------------------------------------------------
// StaticControl implementation
// ---------------------------------------------------------------------------

StaticControl::StaticControl() : hwnd_(NULL) {
}

StaticControl::~StaticControl() {
    Destroy();
}

bool StaticControl::Create(HWND parent, int id, const std::string& text,
                          int x, int y, int width, int height) {
    hwnd_ = CreateWindowExA(
        0,
        "STATIC",
        text.c_str(),
        WS_CHILD | WS_VISIBLE,
        x, y, width, height,
        parent,
        (HMENU)(UINT_PTR)id,
        GetModuleHandle(NULL),
        NULL
    );
    
    return hwnd_ != NULL;
}

void StaticControl::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = NULL;
    }
}

void StaticControl::SetText(const std::string& text) {
    if (hwnd_) {
        SetWindowTextA(hwnd_, text.c_str());
    }
}

std::string StaticControl::GetText() const {
    if (!hwnd_) return "";
    
    int len = GetWindowTextLengthA(hwnd_);
    if (len == 0) return "";
    
    std::string text(len + 1, '\0');
    GetWindowTextA(hwnd_, &text[0], len + 1);
    text.resize(len);
    return text;
}

// ---------------------------------------------------------------------------
// ListBox implementation
// ---------------------------------------------------------------------------

ListBox::ListBox() : hwnd_(NULL) {
}

ListBox::~ListBox() {
    Destroy();
}

bool ListBox::Create(HWND parent, int id,
                   int x, int y, int width, int height) {
    hwnd_ = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "LISTBOX",
        "",
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
        x, y, width, height,
        parent,
        (HMENU)(UINT_PTR)id,
        GetModuleHandle(NULL),
        NULL
    );
    
    return hwnd_ != NULL;
}

void ListBox::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = NULL;
    }
}

int ListBox::AddString(const std::string& text) {
    if (!hwnd_) return LB_ERR;
    return SendMessage(hwnd_, LB_ADDSTRING, 0, (LPARAM)text.c_str());
}

int ListBox::InsertString(int index, const std::string& text) {
    if (!hwnd_) return LB_ERR;
    return SendMessage(hwnd_, LB_INSERTSTRING, index, (LPARAM)text.c_str());
}

int ListBox::DeleteString(int index) {
    if (!hwnd_) return LB_ERR;
    return SendMessage(hwnd_, LB_DELETESTRING, index, 0);
}

void ListBox::ResetContent() {
    if (hwnd_) {
        SendMessage(hwnd_, LB_RESETCONTENT, 0, 0);
    }
}

int ListBox::GetCount() const {
    if (!hwnd_) return 0;
    return SendMessage(hwnd_, LB_GETCOUNT, 0, 0);
}

int ListBox::GetCurSel() const {
    if (!hwnd_) return LB_ERR;
    return SendMessage(hwnd_, LB_GETCURSEL, 0, 0);
}

void ListBox::SetCurSel(int index) {
    if (hwnd_) {
        SendMessage(hwnd_, LB_SETCURSEL, index, 0);
    }
}

std::string ListBox::GetText(int index) const {
    if (!hwnd_) return "";
    
    int len = SendMessage(hwnd_, LB_GETTEXTLEN, index, 0);
    if (len == LB_ERR) return "";
    
    std::string text(len + 1, '\0');
    SendMessage(hwnd_, LB_GETTEXT, index, (LPARAM)&text[0]);
    text.resize(len);
    return text;
}

// ---------------------------------------------------------------------------
// ComboBox implementation
// ---------------------------------------------------------------------------

ComboBox::ComboBox() : hwnd_(NULL) {
}

ComboBox::~ComboBox() {
    Destroy();
}

bool ComboBox::Create(HWND parent, int id,
                    int x, int y, int width, int height, DWORD style) {
    hwnd_ = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "COMBOBOX",
        "",
        style,
        x, y, width, height,
        parent,
        (HMENU)(UINT_PTR)id,
        GetModuleHandle(NULL),
        NULL
    );
    
    return hwnd_ != NULL;
}

void ComboBox::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = NULL;
    }
}

int ComboBox::AddString(const std::string& text) {
    if (!hwnd_) return CB_ERR;
    return SendMessage(hwnd_, CB_ADDSTRING, 0, (LPARAM)text.c_str());
}

int ComboBox::InsertString(int index, const std::string& text) {
    if (!hwnd_) return CB_ERR;
    return SendMessage(hwnd_, CB_INSERTSTRING, index, (LPARAM)text.c_str());
}

int ComboBox::DeleteString(int index) {
    if (!hwnd_) return CB_ERR;
    return SendMessage(hwnd_, CB_DELETESTRING, index, 0);
}

void ComboBox::ResetContent() {
    if (hwnd_) {
        SendMessage(hwnd_, CB_RESETCONTENT, 0, 0);
    }
}

int ComboBox::GetCount() const {
    if (!hwnd_) return 0;
    return SendMessage(hwnd_, CB_GETCOUNT, 0, 0);
}

int ComboBox::GetCurSel() const {
    if (!hwnd_) return CB_ERR;
    return SendMessage(hwnd_, CB_GETCURSEL, 0, 0);
}

void ComboBox::SetCurSel(int index) {
    if (hwnd_) {
        SendMessage(hwnd_, CB_SETCURSEL, index, 0);
    }
}

std::string ComboBox::GetText(int index) const {
    if (!hwnd_) return "";
    
    int len = SendMessage(hwnd_, CB_GETLBTEXTLEN, index, 0);
    if (len == CB_ERR) return "";
    
    std::string text(len + 1, '\0');
    SendMessage(hwnd_, CB_GETLBTEXT, index, (LPARAM)&text[0]);
    text.resize(len);
    return text;
}

std::string ComboBox::GetEditText() const {
    if (!hwnd_) return "";
    
    int len = GetWindowTextLengthA(hwnd_);
    if (len == 0) return "";
    
    std::string text(len + 1, '\0');
    GetWindowTextA(hwnd_, &text[0], len + 1);
    text.resize(len);
    return text;
}

void ComboBox::SetEditText(const std::string& text) {
    if (hwnd_) {
        SetWindowTextA(hwnd_, text.c_str());
    }
}

}  // namespace Engine::Content::Platform::WinAPI::Controls
