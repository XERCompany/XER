// vst3_cef.h — XER_VST3_CEF demo: VST3 editor view + CEF browser view.
//
// One Win32 window split in two panes: the left pane hosts a VST3
// plugin's native editor (IPlugView via Vst3HostInterface), the right
// pane hosts a real Chromium browser (CefHost via cef_runtime). Only
// the repo's public embedding APIs are exercised — no private
// IPlugView/CefClient plumbing is reimplemented here.
#pragma once

#include <cstdint>
#include <string>

namespace XER::Demo {

// Startup switches parsed from the command line.
struct Vst3CefDemoOptions {
    std::string plugin_path;             // optional .vst3 loaded on startup
    std::string url;                     // optional start URL for the CEF view
    std::string audio_path;              // optional audio file loaded on startup
    double sample_rate = 48000.0;
    std::uint32_t max_block_size = 1024;
};

// Creates the demo window and runs its message loop until the window
// is closed. Returns the process exit code. Windows-only.
int RunVst3CefDemo(const Vst3CefDemoOptions& options);

}  // namespace XER::Demo
