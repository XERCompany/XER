#pragma once

#include <string>
#include <vector>

namespace XER::Audio::Plugin {

// Metadata parsed from a bundle's Contents\Resources\moduleinfo.json.
// Only read for bundles under the standard "Common Files\VST3" root.
struct Vst3ModuleInfo {
    std::string name;
    std::string vendor;
    std::string version;
    std::string url;
    std::string email;
    struct ClassEntry {
        std::string cid;        // 32-char hex TUID
        std::string category;   // e.g. "Audio Module Class"
        std::string name;
        std::string vendor;
        std::string version;
        std::vector<std::string> sub_categories;
        std::vector<std::string> snapshots;
    };
    std::vector<ClassEntry> classes;
};

// One discovered VST3 module (bundle directory or single-file module).
struct Vst3PluginInfo {
    std::string path;       // user-facing reference (bundle dir or .vst3 file)
    std::string module_path;// resolved PE binary actually loaded
    std::string name;       // class name from the plugin factory
    std::string vendor;
    std::string category;   // e.g. "Audio Module Class", "Instrument|..."
    std::string version;
    std::string resources_path;  // bundle Contents\Resources dir (if read)
};

// A *.vst3 candidate found under a scan root, before any plugin code runs.
struct Vst3ModuleCandidate {
    std::string path;        // bundle dir or .vst3 file as discovered
    std::string module_path; // resolved PE binary (empty when unresolvable)
    std::string resources_path;  // Contents\Resources dir when loaded
    Vst3ModuleInfo module_info;  // parsed moduleinfo (has_module_info)
    bool has_module_info = false;
};

// Recursively lists *.vst3 modules under the given roots and resolves each
// to its loadable PE binary. No plugin code is loaded — safe to call
// anywhere; the dangerous part (LoadLibrary) is left to ProbeVst3Module.
std::vector<Vst3ModuleCandidate> EnumerateVst3Modules(
    const std::vector<std::string>& roots);

// Loads ONE module binary, reads its preferred class metadata, unloads.
// Runs plugin DllMain in the calling process — crash-risky, so prefer
// calling it from an isolated worker process.
bool ProbeVst3Module(const std::string& module_path, Vst3PluginInfo* info);

// Recursively scans the given roots for *.vst3 modules, loads each module's
// factory and reads class metadata. Loading arbitrary plugin DLLs runs their
// DllMain — call from the UI thread, never the audio thread.
std::vector<Vst3PluginInfo> ScanVst3Plugins(
    const std::vector<std::string>& roots,
    std::string* error_out = nullptr);

// Default scan roots: %CommonProgramFiles%\VST3 and common vendor dirs.
std::vector<std::string> DefaultVst3SearchRoots();

// True when `path` sits under a standard system "Common Files\VST3"
// root — the only location where bundle Resources are loaded.
bool IsCommonFilesVst3Path(const std::wstring& path);

// Maps a resolved module binary path back to its bundle directory:
// ...\Foo.vst3\Contents\<arch>-win\Foo.vst3 -> ...\Foo.vst3.
// Returns the path itself when it is already a bundle directory.
std::wstring Vst3BundleRootForModule(const std::wstring& module_path);

// Reads <bundle>\Contents\Resources\moduleinfo.json. The parser is
// tolerant: moduleinfo files allow comments and trailing commas.
bool ReadVst3ModuleInfo(const std::wstring& bundle_dir,
                        Vst3ModuleInfo* info_out);

}  // namespace XER::Audio::Plugin
