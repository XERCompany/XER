#include "vst3_plugin_scanner.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>

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

// ---- tolerant JSON -------------------------------------------------------
// moduleinfo.json files in the wild carry // and /* */ comments and
// trailing commas — strict parsers reject them, so a small recursive
// reader handles exactly what the file format allows.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text)
        : pos_(text.data()), end_(text.data() + text.size()) {}

    bool Parse(JsonValue* out) {
        SkipWs();
        return pos_ < end_ && ParseValue(out);
    }

private:
    void SkipWs() {
        for (;;) {
            while (pos_ < end_ && std::isspace(
                       static_cast<unsigned char>(*pos_))) {
                ++pos_;
            }
            if (end_ - pos_ >= 2 && pos_[0] == '/' && pos_[1] == '/') {
                while (pos_ < end_ && *pos_ != '\n') ++pos_;
            } else if (end_ - pos_ >= 2 && pos_[0] == '/' &&
                       pos_[1] == '*') {
                pos_ += 2;
                while (end_ - pos_ >= 2 &&
                       !(pos_[0] == '*' && pos_[1] == '/')) {
                    ++pos_;
                }
                if (end_ - pos_ >= 2) pos_ += 2;
            } else {
                return;
            }
        }
    }

    bool ParseString(std::string* out) {
        if (pos_ >= end_ || *pos_ != '"') return false;
        ++pos_;
        out->clear();
        while (pos_ < end_) {
            const char ch = *pos_++;
            if (ch == '"') return true;
            if (ch == '\\' && pos_ < end_) {
                const char esc = *pos_++;
                switch (esc) {
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'u':  // keep the escape; ASCII fields don't emit it
                    pos_ += 4;
                    break;
                default: out->push_back(esc); break;
                }
                continue;
            }
            out->push_back(ch);
        }
        return false;
    }

    bool ParseLiteral(const char* word, JsonValue* v, JsonValue::Type t) {
        const size_t n = std::strlen(word);
        if (static_cast<size_t>(end_ - pos_) < n ||
            std::strncmp(pos_, word, n) != 0) {
            return false;
        }
        pos_ += n;
        v->type = t;
        return true;
    }

    bool ParseValue(JsonValue* v) {
        SkipWs();
        if (pos_ >= end_) return false;
        const char ch = *pos_;
        if (ch == '"') {
            v->type = JsonValue::Type::String;
            return ParseString(&v->str);
        }
        if (ch == '{') {
            ++pos_;
            v->type = JsonValue::Type::Object;
            for (;;) {
                SkipWs();
                if (pos_ < end_ && *pos_ == '}') { ++pos_; return true; }
                std::string key;
                if (!ParseString(&key)) return false;
                SkipWs();
                if (pos_ >= end_ || *pos_ != ':') return false;
                ++pos_;
                JsonValue val;
                if (!ParseValue(&val)) return false;
                v->obj.emplace_back(std::move(key), std::move(val));
                SkipWs();
                if (pos_ < end_ && *pos_ == ',') { ++pos_; continue; }
                if (pos_ < end_ && *pos_ == '}') { ++pos_; return true; }
                return false;
            }
        }
        if (ch == '[') {
            ++pos_;
            v->type = JsonValue::Type::Array;
            for (;;) {
                SkipWs();
                if (pos_ < end_ && *pos_ == ']') { ++pos_; return true; }
                JsonValue val;
                if (!ParseValue(&val)) return false;
                v->arr.push_back(std::move(val));
                SkipWs();
                if (pos_ < end_ && *pos_ == ',') { ++pos_; continue; }
                if (pos_ < end_ && *pos_ == ']') { ++pos_; return true; }
                return false;
            }
        }
        if (ch == 't') {
            if (!ParseLiteral("true", v, JsonValue::Type::Bool)) return false;
            v->boolean = true;
            return true;
        }
        if (ch == 'f') {
            if (!ParseLiteral("false", v, JsonValue::Type::Bool)) return false;
            v->boolean = false;
            return true;
        }
        if (ch == 'n') return ParseLiteral("null", v, JsonValue::Type::Null);
        // number: keep the raw token
        const char* start = pos_;
        while (pos_ < end_ &&
               (std::isdigit(static_cast<unsigned char>(*pos_)) ||
                *pos_ == '-' || *pos_ == '+' || *pos_ == '.' ||
                *pos_ == 'e' || *pos_ == 'E')) {
            ++pos_;
        }
        if (pos_ == start) return false;
        v->type = JsonValue::Type::Number;
        v->str.assign(start, pos_);
        return true;
    }

    const char* pos_;
    const char* end_;
};

const JsonValue* JsonGet(const JsonValue& v, const char* key) {
    if (v.type != JsonValue::Type::Object) return nullptr;
    for (const auto& [k, val] : v.obj) {
        if (k == key) return &val;
    }
    return nullptr;
}

std::string JsonStr(const JsonValue& v, const char* key) {
    const JsonValue* val = JsonGet(v, key);
    return val != nullptr && val->type == JsonValue::Type::String
               ? val->str : std::string();
}

std::vector<std::string> JsonStrList(const JsonValue& v, const char* key) {
    std::vector<std::string> out;
    const JsonValue* val = JsonGet(v, key);
    if (val != nullptr && val->type == JsonValue::Type::Array) {
        for (const auto& item : val->arr) {
            if (item.type == JsonValue::Type::String) {
                out.push_back(item.str);
            }
        }
    }
    return out;
}

// ---- path helpers --------------------------------------------------------

std::wstring PathParent(const std::wstring& p) {
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

std::wstring PathLeaf(const std::wstring& p) {
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

std::wstring PathNormalize(std::wstring p) {
    for (wchar_t& c : p) {
        c = (c == L'/') ? L'\\' : static_cast<wchar_t>(towlower(c));
    }
    while (!p.empty() && p.back() == L'\\') {
        p.pop_back();
    }
    return p;
}

std::vector<std::wstring> CommonFilesVst3Roots() {
    std::vector<std::wstring> roots;
    wchar_t buf[MAX_PATH]{};
    const DWORD len =
        GetEnvironmentVariableW(L"CommonProgramFiles", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        roots.emplace_back(std::wstring(buf) + L"\\VST3");
    }
    roots.emplace_back(L"C:\\Program Files\\Common Files\\VST3");
    return roots;
}

bool IsVst3ArchDir(const std::wstring& leaf_lower) {
    return leaf_lower == L"x86_64-win" || leaf_lower == L"arm64-win" ||
           leaf_lower == L"x86-win" || leaf_lower == L"i386-win" ||
           leaf_lower == L"x86_64-linux" ||
           leaf_lower == L"armv7l-linux" || leaf_lower == L"arm64-linux" ||
           leaf_lower == L"macos" || leaf_lower == L"macos-arm64";
}

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

    // The IPtr destructor calls release() on an object living inside the
    // module — that virtual call must happen while the code is still
    // mapped, so release it BEFORE FreeLibrary or every successful probe
    // crashes. (factory2 already dies inside its inner scope.)
    factory = nullptr;
    FreeLibrary(dll);
    return ok;
}

// Overlays moduleinfo.json fields onto a probed plugin info — only for
// bundles under the standard Common Files\VST3 root.
void MergeModuleInfo(const std::wstring& module_binary,
                     Vst3PluginInfo* info) {
    const std::wstring bundle = Vst3BundleRootForModule(module_binary);
    if (!IsCommonFilesVst3Path(bundle)) {
        return;
    }
    Vst3ModuleInfo mi;
    if (!ReadVst3ModuleInfo(bundle, &mi)) {
        return;
    }
    if (!mi.name.empty()) info->name = mi.name;
    if (!mi.vendor.empty()) info->vendor = mi.vendor;
    if (!mi.version.empty()) info->version = mi.version;
    for (const auto& cls : mi.classes) {
        if (cls.category == "Audio Module Class" ||
            cls.category == "Instrument") {
            info->category = cls.category;
            if (!cls.sub_categories.empty()) {
                info->category += "|" + cls.sub_categories.front();
            }
            break;
        }
    }
    info->resources_path = XER::Helper::String::WideToUtf8(
        bundle + L"\\Contents\\Resources");
}

}  // namespace

bool IsCommonFilesVst3Path(const std::wstring& path) {
    const std::wstring norm = PathNormalize(path);
    for (std::wstring root : CommonFilesVst3Roots()) {
        root = PathNormalize(root);
        if (norm.size() > root.size() &&
            norm.compare(0, root.size(), root) == 0 &&
            norm[root.size()] == L'\\') {
            return true;
        }
    }
    return false;
}

std::wstring Vst3BundleRootForModule(const std::wstring& module_path) {
    const DWORD attrs = GetFileAttributesW(module_path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return module_path;
    }
    const std::wstring dir = PathParent(module_path);
    std::wstring leaf = PathLeaf(dir);
    std::transform(leaf.begin(), leaf.end(), leaf.begin(), towlower);
    if (IsVst3ArchDir(leaf)) {
        const std::wstring contents = PathParent(dir);
        std::wstring contents_leaf = PathLeaf(contents);
        std::transform(contents_leaf.begin(), contents_leaf.end(),
                       contents_leaf.begin(), towlower);
        if (contents_leaf == L"contents") {
            return PathParent(contents);
        }
    }
    return dir;   // loose binary — nearest containing directory
}

bool ReadVst3ModuleInfo(const std::wstring& bundle_dir,
                        Vst3ModuleInfo* info_out) {
    if (info_out == nullptr) {
        return false;
    }
    const std::wstring file =
        bundle_dir + L"\\Contents\\Resources\\moduleinfo.json";
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size {};
    std::string text;
    if (GetFileSizeEx(handle, &size) != 0 && size.QuadPart > 0 &&
        size.QuadPart < (1 << 20)) {   // moduleinfo is a few KB
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ReadFile(handle, text.data(), static_cast<DWORD>(text.size()),
                 &read, nullptr);
        text.resize(read);
    }
    CloseHandle(handle);
    if (text.empty()) {
        return false;
    }

    JsonValue root;
    if (!JsonReader(text).Parse(&root) ||
        root.type != JsonValue::Type::Object) {
        return false;
    }

    info_out->name = JsonStr(root, "Name");
    info_out->version = JsonStr(root, "Version");
    if (const JsonValue* factory = JsonGet(root, "Factory Info")) {
        info_out->vendor = JsonStr(*factory, "Vendor");
        info_out->url = JsonStr(*factory, "URL");
        info_out->email = JsonStr(*factory, "E-Mail");
    }
    if (const JsonValue* classes = JsonGet(root, "Classes")) {
        if (classes->type == JsonValue::Type::Array) {
            for (const auto& entry : classes->arr) {
                Vst3ModuleInfo::ClassEntry cls;
                cls.cid = JsonStr(entry, "CID");
                cls.category = JsonStr(entry, "Category");
                cls.name = JsonStr(entry, "Name");
                cls.vendor = JsonStr(entry, "Vendor");
                cls.version = JsonStr(entry, "Version");
                cls.sub_categories = JsonStrList(entry, "Sub Categories");
                // Snapshots entries are {"Scale Factor":1,"Path":"..."}
                if (const JsonValue* snaps = JsonGet(entry, "Snapshots")) {
                    if (snaps->type == JsonValue::Type::Array) {
                        for (const auto& snap : snaps->arr) {
                            const std::string path = JsonStr(snap, "Path");
                            if (!path.empty()) {
                                cls.snapshots.push_back(path);
                            }
                        }
                    }
                }
                info_out->classes.push_back(std::move(cls));
            }
        }
    }
    return !info_out->name.empty() || !info_out->classes.empty();
}

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

std::vector<Vst3ModuleCandidate> EnumerateVst3Modules(
    const std::vector<std::string>& roots) {
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

    std::vector<Vst3ModuleCandidate> result;
    result.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        Vst3ModuleCandidate entry;
        entry.path = XER::Helper::String::WideToUtf8(candidate);
        entry.module_path =
            XER::Helper::String::WideToUtf8(ResolveModuleBinary(candidate));
        // Bundles under the system VST3 dir carry Resources — read the
        // moduleinfo metadata while we are here (no plugin code runs).
        if (IsCommonFilesVst3Path(candidate)) {
            const std::wstring res =
                candidate + L"\\Contents\\Resources";
            if (GetFileAttributesW(res.c_str()) != INVALID_FILE_ATTRIBUTES) {
                entry.resources_path =
                    XER::Helper::String::WideToUtf8(res);
            }
            entry.has_module_info =
                ReadVst3ModuleInfo(candidate, &entry.module_info);
        }
        result.push_back(std::move(entry));
    }
    return result;
}

bool ProbeVst3Module(const std::string& module_path, Vst3PluginInfo* info) {
    if (info == nullptr) {
        return false;
    }
    const std::wstring binary = XER::Helper::String::Utf8ToWide(module_path);
    if (binary.empty()) {
        return false;
    }
    const bool ok = ProbeModule(binary, info);
    if (ok) {
        MergeModuleInfo(binary, info);
    }
    return ok;
}

std::vector<Vst3PluginInfo> ScanVst3Plugins(const std::vector<std::string>& roots,
                                            std::string* error_out) {
    std::vector<Vst3PluginInfo> result;
    const auto candidates = EnumerateVst3Modules(roots);

    for (const auto& candidate : candidates) {
        if (candidate.module_path.empty()) {
            continue;
        }
        Vst3PluginInfo info;
        info.path = candidate.path;
        info.module_path = candidate.module_path;
        if (ProbeVst3Module(candidate.module_path, &info)) {
            result.push_back(std::move(info));
        }
    }

    if (error_out && result.empty() && candidates.empty()) {
        *error_out = "no .vst3 modules found in search roots";
    }
    return result;
}

}  // namespace XER::Audio::Plugin
