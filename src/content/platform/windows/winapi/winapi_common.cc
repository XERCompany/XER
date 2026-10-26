#include "winapi_common.h"
#include <comdef.h>

namespace Engine::Content::Platform::WinAPI {

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

std::string WinAPIError::GetLastErrorString() {
    return FormatError(::GetLastError());
}

std::string WinAPIError::FormatError(DWORD errorCode) {
    LPSTR messageBuffer = nullptr;
    
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer,
        0,
        NULL
    );
    
    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);
    
    return message;
}

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

bool WindowClassRegistrar::RegisterDefaultClass(HINSTANCE hInstance) {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "XERDefaultWindow";
    
    return RegisterClassExA(&wc) != 0;
}

ATOM WindowClassRegistrar::RegisterCustomClass(HINSTANCE hInstance,
                                               const std::string& className,
                                               WNDPROC wndProc,
                                               UINT style) {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = style;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = className.c_str();
    
    return RegisterClassExA(&wc);
}

// ---------------------------------------------------------------------------
// Message loop
// ---------------------------------------------------------------------------

void MessageLoop::Run() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void MessageLoop::RunWithHandler(MessageHandler handler) {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!handler || !handler(msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

void MessageLoop::PumpMessages() {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool MessageLoop::PeekAndDispatch() {
    MSG msg;
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Resource helpers
// ---------------------------------------------------------------------------

HICON ResourceHelper::LoadIcon(int resourceId) {
    return ::LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(resourceId));
}

HCURSOR ResourceHelper::LoadCursor(int resourceId) {
    return ::LoadCursor(GetModuleHandle(NULL), MAKEINTRESOURCE(resourceId));
}

HBITMAP ResourceHelper::LoadBitmap(int resourceId) {
    return ::LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(resourceId));
}

std::string ResourceHelper::LoadString(int resourceId) {
    char buffer[256];
    if (::LoadString(GetModuleHandle(NULL), resourceId, buffer, sizeof(buffer))) {
        return std::string(buffer);
    }
    return "";
}

}  // namespace Engine::Content::Platform::WinAPI
