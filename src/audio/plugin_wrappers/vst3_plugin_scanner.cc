#include "vst3_plugin_scanner.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

namespace XER::Audio::Plugin {
namespace {

using namespace Steinberg;
using namespace Steinberg::Vst;

// Resolves a .vst3 candidate (bundle dir or module file) to its PE binary.
std::wstring ResolveModuleBinary(const std::wstring& candidate) {
    const DWORD attrs = GetFileAttributesW(candidate.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return {};
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return candidate;
    }
    // Bundle: look inside Contents/<arch>-win for the binary.
    static const wchar_t* kArchDirs[] = {L"x86_64-win", L"arm64-win"};
    for (const wchar_t* arch : kArchDirs) {
        const std::wstring arch_dir = candidate + L"\\Contents\\" + arch;
        const DWORD arch_attrs = GetFileAttributesW(arch_dir.c_str());
        if (arch_attrs == INVALID_FILE_ATTRIBUTES ||
            (arch_attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            continue;
        }
        WIN32_FIND_DATAW fd {};
        HANDLE find = FindFirstFileW((arch_dir + L"\\*.vst3").c_str(), &fd);
        if (find != INVALID_HANDLE_VALUE) {
            std::wstring found = arch_dir + L"\\" + fd.cFileName;
            FindClose(find);
            return found;
        }
    }
    // Fallback: any *.vst3 directly inside the bundle root.
    WIN32_FIND_DATAW fd {};
    HANDLE find = FindFirstFileW((candidate + L"\\*.vst3").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        std::wstring found = candidate + L"\\" + fd.cFileName;
        FindClose(find);
        return found;
    }
    return {};
}

void CollectCandidates(const std::wstring& dir, std::vector<std::wstring>& out) {
    const std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == L'\0' ||
             (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0'))) {
            continue;
        }
        const std::wstring full = dir + L"\\" + fd.cFileName;
        const bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (is_dir) {
            // Recurse into bundle dirs and regular folders alike; only
            // *.vst3-named entries are collected as plugins.
            if (XER::Helper::String::EndsWith(fd.cFileName, L".vst3") ||
                XER::Helper::String::EndsWith(fd.cFileName, L".VST3")) {
                out.push_back(full);
            } else {
                CollectCandidates(full, out);
            }
        } else if (XER::Helper::String::EndsWith(fd.cFileName, L".vst3") ||
                   XER::Helper::String::EndsWith(fd.cFileName, L".VST3")) {
            out.push_back(full);
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

bool IsAudioClassCategory(const char* category) {
    return std::strcmp(category, kVstAudioEffectClass) == 0 ||
           std::strncmp(category, PlugType::kInstrument,
                        std::strlen(PlugType::kInstrument)) == 0;
}

// Loads one module binary, reads its preferred class info, unloads.
bool ProbeModule(const std::wstring& module_binary, Vst3PluginInfo* info) {
    HMODULE dll = LoadLibraryW(module_binary.c_str());
    if (dll == nullptr) {
        return false;
    }
    using GetPluginFactoryProc = IPluginFactory* (*)();
    const auto get_factory = reinterpret_cast<GetPluginFactoryProc>(
        GetProcAddress(dll, "GetPluginFactory"));
    FUnknown* factory_unknown = get_factory ? get_factory() : nullptr;
    IPtr<IPluginFactory> factory;
    if (factory_unknown != nullptr) {
        IPluginFactory* raw = nullptr;
        factory_unknown->queryInterface(IPluginFactory::iid,
                                        reinterpret_cast<void**>(&raw));
        factory_unknown->release();
        factory = IPtr<IPluginFactory>::adopt(raw);
    }

    bool ok = false;
    if (factory) {
        // Prefer audio/instrument classes; fall back to class 0.
        const int32 count = factory->countClasses();
        int32 selected = -1;
        for (int32 i = 0; i < count; ++i) {
            PClassInfo ci {};
            if (factory->getClassInfo(i, &ci) != kResultOk) {
                continue;
            }
            if (IsAudioClassCategory(ci.category)) {
                selected = i;
                break;
            }
            if (selected < 0) {
                selected = i;
            }
        }
        if (selected >= 0) {
            // Rich info via IPluginFactory2 when available.
            FUnknown* f2_unknown = nullptr;
            factory->queryInterface(IPluginFactory2::iid,
                                    reinterpret_cast<void**>(&f2_unknown));
            IPtr<IPluginFactory2> factory2 =
                IPtr<IPluginFactory2>::adopt(
                    reinterpret_cast<IPluginFactory2*>(f2_unknown));

            PClassInfo2 ci2 {};
            PClassInfo ci {};
            const bool has2 = factory2 &&
                factory2->getClassInfo2(selected, &ci2) == kResultOk;
            if (!has2) {
                factory->getClassInfo(selected, &ci);
            }
            info->name = has2 ? std::string(reinterpret_cast<const char*>(ci2.name))
                              : std::string(reinterpret_cast<const char*>(ci.name));
            info->category = has2
                ? std::string(reinterpret_cast<const char*>(ci2.category))
                : std::string(reinterpret_cast<const char*>(ci.category));
            info->vendor = has2
                ? std::string(reinterpret_cast<const char*>(ci2.vendor))
                : std::string();
            info->version = has2
                ? std::string(reinterpret_cast<const char*>(ci2.version))
                : std::string();
            ok = !info->name.empty();
        }
    }

    FreeLibrary(dll);
    return ok;
}

}  // namespace

std::vector<std::string> DefaultVst3SearchRoots() {
    std::vector<std::string> roots;
    char buf[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableA("CommonProgramFiles", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        roots.emplace_back(std::string(buf) + "\\VST3");
    }
    roots.emplace_back("C:\\Program Files\\Common Files\\VST3");
    return roots;
}

std::vector<Vst3PluginInfo> ScanVst3Plugins(const std::vector<std::string>& roots,
                                            std::string* error_out) {
    std::vector<Vst3PluginInfo> result;
    std::vector<std::wstring> candidates;

    for (const auto& root : roots) {
        const std::wstring wide_root = XER::Helper::String::Utf8ToWide(root);
        if (wide_root.empty()) {
            continue;
        }
        if (GetFileAttributesW(wide_root.c_str()) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        CollectCandidates(wide_root, candidates);
    }

    // De-duplicate by resolved module path.
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());

    for (const auto& candidate : candidates) {
        const std::wstring binary = ResolveModuleBinary(candidate);
        if (binary.empty()) {
            continue;
        }
        Vst3PluginInfo info;
        info.path = XER::Helper::String::WideToUtf8(candidate);
        info.module_path = XER::Helper::String::WideToUtf8(binary);
        if (ProbeModule(binary, &info)) {
            result.push_back(std::move(info));
        }
    }

    if (error_out && result.empty() && candidates.empty()) {
        *error_out = "no .vst3 modules found in search roots";
    }
    return result;
}

}  // namespace XER::Audio::Plugin
