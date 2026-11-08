#include "clipboard.h"
#include "helper/string_utils.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstring>

namespace XER::Content::Clipboard {

bool SetText(std::string_view text) {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    // UTF-16 payload + NUL terminator.
    const std::wstring wide = XER::Helper::String::Utf8ToWide(text);
    if (wide.empty() && !text.empty()) {
        CloseClipboard();
        return false;
    }
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (handle == nullptr) {
        CloseClipboard();
        return false;
    }

    if (void* ptr = GlobalLock(handle)) {
        if (!wide.empty()) {
            std::memcpy(ptr, wide.data(), wide.size() * sizeof(wchar_t));
        }
        static_cast<wchar_t*>(ptr)[wide.size()] = L'\0';
        GlobalUnlock(handle);
    } else {
        GlobalFree(handle);
        CloseClipboard();
        return false;
    }

    const bool ok = SetClipboardData(CF_UNICODETEXT, handle) != nullptr;
    CloseClipboard();
    // On failure Windows did not take ownership — free ourselves.
    if (!ok) {
        GlobalFree(handle);
    }
    return ok;
#else
    (void)text;
    return false;
#endif
}

bool GetText(std::string& text_out) {
#ifdef _WIN32
    text_out.clear();
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    const HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle == nullptr) {
        CloseClipboard();
        return false;
    }
    const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(handle));
    if (wide == nullptr) {
        CloseClipboard();
        return false;
    }

    const int wide_len = static_cast<int>(std::wcslen(wide));
    text_out = XER::Helper::String::WideToUtf8(
        std::wstring_view(wide, static_cast<size_t>(wide_len)));
    GlobalUnlock(handle);
    CloseClipboard();
    return true;
#else
    return false;
#endif
}

} // namespace XER::Content::Clipboard
