#include "lut_manager.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "app_runtime/app_runtime.h"
#include "color/ocio_transform.h"
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "media_catalog.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // GetModuleFileNameW — exe-dir search dirs
#endif

namespace XER::VideoEditor::Lut {
namespace {

namespace Path = XER::Helper::Path;

// Case-insensitive extension test — scan accepts .cube/.3dl/.look
// (OCIO FileTransform reads all three; only .cube gets the local parse).
bool IsLutFile(const std::filesystem::path& p) {
    const std::string ext = XER::Helper::String::ToLowerAscii(
        p.extension().string());
    return ext == ".cube" || ext == ".3dl" || ext == ".look";
}

// Case-folded key for name dedupe (FindOcioProfile is also
// case-insensitive, so "Fuji" and "FUJI" are the same profile).
std::string FoldName(const std::string& name) {
    return XER::Helper::String::ToLowerAscii(name);
}

// Strips one pair of surrounding quotes and trims — .cube TITLE values
// arrive either bare or "quoted".
std::string Unquote(std::string text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t");
    text = text.substr(first, last - first + 1);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

// Generic-separator UTF-8 form — the string stored on Entry.path and
// on the OcioProfile.lut_path registration (FindByPath compares it).
std::string CanonicalLutPath(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path canon = std::filesystem::weakly_canonical(p, ec);
    if (ec || canon.empty()) {
        canon = std::filesystem::absolute(p, ec).lexically_normal();
        if (ec || canon.empty()) canon = p;
    }
    return Path::ToUtf8(canon);
}

// Executable's own directory — the fallback assets probe for builds
// where AppRuntime::Boot did not resolve an assets dir.
std::filesystem::path ExeDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : self.parent_path();
#endif
}

// <user_data>/XER/VideoEditor — the same env chain the catalog's
// DefaultDbPath() walks (LOCALAPPDATA → XDG_DATA_HOME → HOME/.local/
// share → temp), parented off the db filename.
std::filesystem::path UserDataDir() {
    return Path::FromUtf8(MediaCatalog::DefaultDbPath()).parent_path();
}

}  // namespace

bool ParseCubeText(const std::string& text, CubeData* out,
                   std::string* error) {
    const auto fail = [error](const std::string& msg) {
        if (error != nullptr) *error = msg;
        return false;
    };
    CubeData data;
    int data_rows = 0;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        // '#' starts a comment anywhere on the line.
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream tokens(line);
        std::string keyword;
        if (!(tokens >> keyword)) continue;   // blank line
        if (keyword == "TITLE") {
            std::string rest;
            std::getline(tokens, rest);
            data.title = Unquote(rest);
        } else if (keyword == "LUT_3D_SIZE") {
            tokens >> data.size;
            data.is_3d = true;
        } else if (keyword == "LUT_1D_SIZE") {
            tokens >> data.size;
            data.is_3d = false;
        } else if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
            // Accepted + ignored — the editor works on normalized 0..1.
            continue;
        } else {
            // Data row: "r g b" — the keyword token is the first float.
            char* end = nullptr;
            const float r = std::strtof(keyword.c_str(), &end);
            if (end == keyword.c_str() || *end != '\0') {
                continue;   // unknown keyword — tolerated
            }
            float g = 0.0f, b = 0.0f;
            if (!(tokens >> g >> b)) continue;
            data.data.push_back(r);
            data.data.push_back(g);
            data.data.push_back(b);
            ++data_rows;
        }
    }
    if (data.size <= 0) {
        return fail("no LUT_3D_SIZE/LUT_1D_SIZE declared");
    }
    if (data_rows == 0) {
        return fail("no data rows");
    }
    // A short/truncated body is tolerated (the file may keep padding),
    // but a wildly inconsistent row count usually means a parse miss —
    // surface it through `error` while still succeeding.
    const std::int64_t expected =
        data.is_3d ? static_cast<std::int64_t>(data.size) * data.size *
                         data.size
                   : static_cast<std::int64_t>(data.size);
    if (error != nullptr && data_rows != expected) {
        *error = "expected " + std::to_string(expected) + " data rows, got " +
                 std::to_string(data_rows);
    }
    if (out != nullptr) *out = std::move(data);
    return true;
}

bool ParseCubeFile(const std::filesystem::path& path, CubeData* out,
                   std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        if (error != nullptr) {
            *error = "cannot open " + Path::ToUtf8(path);
        }
        return false;
    }
    std::ostringstream body;
    body << in.rdbuf();
    return ParseCubeText(body.str(), out, error);
}

Manager& Manager::Shared() {
    static Manager manager;
    return manager;
}

void Manager::SetProjectDir(const std::filesystem::path& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    project_dir_ = dir;
}

std::vector<std::filesystem::path> Manager::SearchDirs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::filesystem::path> dirs;
    const auto add = [&dirs](std::filesystem::path dir) {
        if (dir.empty()) return;
        // Dedupe by canonical path so the same physical dir is never
        // scanned twice (assets_dir and exe_dir coincide in deploys).
        std::error_code ec;
        const std::filesystem::path canon =
            std::filesystem::weakly_canonical(dir, ec);
        const std::filesystem::path& key = ec ? dir : canon;
        if (std::find(dirs.begin(), dirs.end(), key) == dirs.end()) {
            dirs.push_back(key);
        }
    };
    if (!project_dir_.empty()) add(project_dir_ / "luts");
    const std::filesystem::path& assets =
        AppRuntime::SharedAssetsConfig().assets_dir;
    if (!assets.empty()) add(assets / "luts");
    const std::filesystem::path exe = ExeDir();
    if (!exe.empty()) {
        add(exe / "assets" / "luts");
        add(exe / "luts");
    }
    const std::filesystem::path user = UserDataDir();
    if (!user.empty()) add(user / "luts");
    return dirs;
}

void Manager::ScanDir(const std::filesystem::path& dir,
                      std::unordered_set<std::string>* used_names,
                      std::vector<Entry>* out) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return;   // missing/unreadable dir is fine — most are absent

    // Sort for a deterministic scan order — the "first dir wins" name
    // rule must not depend on filesystem iteration order.
    std::vector<std::filesystem::path> files;
    for (const auto& de : it) {
        std::error_code rec;
        if (!de.is_regular_file(rec)) continue;
        if (IsLutFile(de.path())) files.push_back(de.path());
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        Entry entry;
        entry.path = CanonicalLutPath(file);
        const std::string ext = XER::Helper::String::ToLowerAscii(
            file.extension().string());
        entry.format = ext.empty() ? "" : ext.substr(1);
        entry.source_dir = Path::ToUtf8(dir);
        entry.name = file.stem().string();
        if (ext == ".cube") {
            // Parse for TITLE/size; a failed parse still lists the file
            // (OCIO may read it anyway — the panel shows "unparsed").
            const CubeData* cube = CubeFor(entry.path);
            if (cube != nullptr) {
                if (!cube->title.empty()) entry.name = cube->title;
                entry.size = cube->size;
                entry.is_3d = cube->is_3d;
            }
        }
        if (entry.name.empty()) entry.name = "LUT";
        // Claim a unique display name within this scan — later dirs and
        // later files lose ties rather than shadowing each other.
        std::string base = entry.name;
        for (int suffix = 2;
             used_names->count(FoldName(entry.name)) != 0; ++suffix) {
            entry.name = base + " (" + std::to_string(suffix) + ")";
        }
        used_names->insert(FoldName(entry.name));
        out->push_back(std::move(entry));
    }
}

std::size_t Manager::Rescan() {
    // Scan into a local list first — CubeFor takes the lock internally,
    // so holding mutex_ across the directory walk would deadlock.
    std::vector<Entry> fresh_entries;
    {
        std::unordered_set<std::string> used_names;
        for (const auto& dir : SearchDirs()) {
            ScanDir(dir, &used_names, &fresh_entries);
        }
    }

    // Re-publish under the profile-table mutex: drop the registrations
    // from the previous scan (matched by their absolute lut_path —
    // never touch yaml-owned entries) and append the fresh list. Names
    // already owned by an existing profile (e.g. the color.yaml ones
    // pointing at the same files) keep their registration —
    // FindOcioProfile resolves the first match, so an existing profile
    // continues to win its name.
    Color::MutateOcioProfiles(
        [this, &fresh_entries](std::vector<Color::OcioProfile>& profiles) {
            profiles.erase(
                std::remove_if(
                    profiles.begin(), profiles.end(),
                    [this](const Color::OcioProfile& p) {
                        return registered_paths_.count(p.lut_path) != 0;
                    }),
                profiles.end());
            registered_paths_.clear();
            for (const Entry& entry : fresh_entries) {
                bool name_taken = false;
                for (const auto& p : profiles) {
                    if (FoldName(p.name) == FoldName(entry.name)) {
                        name_taken = true;
                        break;
                    }
                }
                if (name_taken) continue;
                Color::OcioProfile profile;
                profile.name = entry.name;
                profile.lut_path = entry.path;  // absolute — applied as-is
                profiles.push_back(std::move(profile));
                registered_paths_.insert(entry.path);
            }
        });
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_ = std::move(fresh_entries);
        return entries_.size();
    }
}

const Entry* Manager::FindByPath(const std::string& utf8_path) const {
    if (utf8_path.empty()) return nullptr;
    // Same canonicalization the scan applied — either separator style
    // and relative inputs resolve to the stored absolute form.
    const std::string want = CanonicalLutPath(
        Path::FromUtf8(Path::NormalizeUtf8Path(utf8_path)));
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Entry& entry : entries_) {
        if (entry.path == want) return &entry;
    }
    return nullptr;
}

const CubeData* Manager::CubeFor(const std::string& utf8_path) {
    // Cache check under the lock, parse unlocked: a duplicated parse on
    // a rare race is harmless — the published pointer stays stable.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = cubes_.find(utf8_path);
        if (it != cubes_.end()) return it->second.get();
    }
    CubeData parsed;
    std::string error;
    std::shared_ptr<CubeData> cube;
    if (ParseCubeFile(Path::FromUtf8(utf8_path), &parsed, &error)) {
        cube = std::make_shared<CubeData>(std::move(parsed));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = cubes_.emplace(utf8_path, cube);
    if (!inserted && it->second != cube) {
        // Another thread finished the same parse first — keep the
        // already-published result for pointer stability.
        return it->second.get();
    }
    return it->second.get();
}

}  // namespace XER::VideoEditor::Lut
