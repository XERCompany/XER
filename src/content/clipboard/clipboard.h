#pragma once

#include <string>
#include <string_view>

namespace XER::Content::Clipboard {

// System clipboard text access (Unicode). Returns false when the clipboard
// is unavailable or held by another process — callers should retry or ignore.
bool SetText(std::string_view text);
bool GetText(std::string& text_out);

} // namespace XER::Content::Clipboard
