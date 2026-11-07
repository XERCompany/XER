#pragma once

#include <string>
#include <vector>

namespace XER::Engine::Audio::Plugin {

// One discovered VST3 module (bundle directory or single-file module).
struct Vst3PluginInfo {
    std::string path;       // user-facing reference (bundle dir or .vst3 file)
    std::string module_path;// resolved PE binary actually loaded
    std::string name;       // class name from the plugin factory
    std::string vendor;
    std::string category;   // e.g. "Audio Module Class", "Instrument|..."
    std::string version;
};

// Recursively scans the given roots for *.vst3 modules, loads each module's
// factory and reads class metadata. Loading arbitrary plugin DLLs runs their
// DllMain — call from the UI thread, never the audio thread.
std::vector<Vst3PluginInfo> ScanVst3Plugins(
    const std::vector<std::string>& roots,
    std::string* error_out = nullptr);

// Default scan roots: %CommonProgramFiles%\VST3 and common vendor dirs.
std::vector<std::string> DefaultVst3SearchRoots();

}  // namespace XER::Engine::Audio::Plugin
