#pragma once

#include <windows.h>
#include <string>
#include <functional>
#include <memory>

namespace Engine::Content::Platform::WinAPI {

// Error handling
class WinAPIError {
public:
    static std::string GetLastErrorString();
    static std::string FormatError(DWORD errorCode);
};

// Window class registration helper
class WindowClassRegistrar {
public:
    static bool RegisterDefaultClass(HINSTANCE hInstance);
    static ATOM RegisterCustomClass(HINSTANCE hInstance, 
                                    const std::string& className,
                                    WNDPROC wndProc,
                                    UINT style = CS_HREDRAW | CS_VREDRAW);
};

// Message loop
class MessageLoop {
public:
    using MessageHandler = std::function<bool(MSG& msg)>;
    
    static void Run();
    static void RunWithHandler(MessageHandler handler);
    static void PumpMessages();
    static bool PeekAndDispatch();
};

// Resource helpers
class ResourceHelper {
public:
    static HICON LoadIcon(int resourceId);
    static HCURSOR LoadCursor(int resourceId);
    static HBITMAP LoadBitmap(int resourceId);
    static std::string LoadString(int resourceId);
};

}  // namespace Engine::Content::Platform::WinAPI
