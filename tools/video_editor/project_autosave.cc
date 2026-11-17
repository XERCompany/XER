// project_autosave.cc — see project_autosave.h for the contract. All
// filesystem work goes through std::error_code / Path::FromUtf8 so a
// non-UTF8-safe locale or a vanished directory degrades to false +
// error_out, never an exception.
#include "project_autosave.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

#include "helper/path_utils.h"
#include "project_store.h"

namespace XER::VideoEditor {
namespace {

namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

// Filename stem used in slot names: "movie.xerproj" -> "movie".
// Falls back to the raw filename, then "project", so the generated
// slot name is never empty or dot-leading.
std::string BasenameFor(const std::string& utf8_project_path) {
    const std::filesystem::path p = Path::FromUtf8(utf8_project_path);
    std::string name = Path::ToUtf8(p.filename().stem());
    if (name.empty()) {
        name = Path::ToUtf8(p.filename());
    }
    if (name.empty()) {
        name = "project";
    }
    return name;
}

// Parses "<basename>.autosave-<N>.xerproj" -> N (>= 1) or -1 when the
// name does not match this project's slot pattern exactly.
int SlotFromFilename(const std::string& utf8_name,
                     const std::string& basename) {
    const std::string prefix = basename + ".autosave-";
    const std::string_view suffix = ".xerproj";
    if (utf8_name.size() <= prefix.size() + suffix.size()) {
        return -1;
    }
    if (utf8_name.compare(0, prefix.size(), prefix) != 0) {
        return -1;
    }
    if (utf8_name.compare(utf8_name.size() - suffix.size(),
                          suffix.size(), suffix) != 0) {
        return -1;
    }
    const std::string_view digits(
        utf8_name.data() + prefix.size(),
        utf8_name.size() - prefix.size() - suffix.size());
    int slot = -1;
    const auto res =
        std::from_chars(digits.data(), digits.data() + digits.size(), slot);
    if (res.ec != std::errc() ||
        res.ptr != digits.data() + digits.size() || slot < 1) {
        return -1;
    }
    return slot;
}

}  // namespace

std::string AutosaveManager::AutosavePathFor(
    const std::string& utf8_project_path,
    const std::string& utf8_autosave_dir, int slot) {
    const std::filesystem::path p =
        Path::FromUtf8(utf8_autosave_dir) /
        (BasenameFor(utf8_project_path) + ".autosave-" +
         std::to_string(slot) + ".xerproj");
    return Path::ToUtf8(p);
}

bool AutosaveManager::Init(const std::string& utf8_project_path,
                           const std::string& utf8_autosave_dir,
                           std::string* error_out) {
    if (utf8_project_path.empty()) {
        return Fail(error_out, "autosave Init: project path is empty");
    }
    if (utf8_autosave_dir.empty()) {
        return Fail(error_out, "autosave Init: autosave dir is empty");
    }
    const std::filesystem::path dir = Path::FromUtf8(utf8_autosave_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return Fail(error_out,
                    "autosave Init: cannot create '" + utf8_autosave_dir +
                        "': " + ec.message());
    }
    if (!std::filesystem::is_directory(dir, ec) || ec) {
        return Fail(error_out, "autosave Init: '" + utf8_autosave_dir +
                                   "' is not a directory");
    }
    project_path_ = utf8_project_path;
    autosave_dir_ = utf8_autosave_dir;
    dirty_ = false;
    has_saved_ = false;
    last_save_ms_ = 0;
    initialized_ = true;
    return true;
}

bool AutosaveManager::MaybeAutosave(const Project& project,
                                    std::int64_t now_ms,
                                    std::string* error_out) {
    if (!initialized_) {
        return Fail(error_out, "AutosaveManager::Init has not been called");
    }
    // Gated calls are success no-ops — the contract distinguishes
    // "nothing to do" from "could not write".
    if (!dirty_) {
        return true;
    }
    if (has_saved_ && now_ms - last_save_ms_ < interval_ms_) {
        return true;
    }

    // The dir may have been deleted since Init — best-effort recreate;
    // a real failure surfaces at ProjectStore::Save below.
    std::error_code ec;
    std::filesystem::create_directories(Path::FromUtf8(autosave_dir_), ec);

    // Rotate: drop the oldest slot, shift N -> N+1 for N = max-1..1 so
    // slot 1 is free for the new copy.
    ec.clear();
    std::filesystem::remove(
        Path::FromUtf8(
            AutosavePathFor(project_path_, autosave_dir_, kMaxAutosaves)),
        ec);
    for (int n = kMaxAutosaves - 1; n >= 1; --n) {
        const std::filesystem::path from = Path::FromUtf8(
            AutosavePathFor(project_path_, autosave_dir_, n));
        const std::filesystem::path to = Path::FromUtf8(
            AutosavePathFor(project_path_, autosave_dir_, n + 1));
        ec.clear();
        if (!std::filesystem::exists(from, ec) || ec) {
            continue;
        }
        std::filesystem::rename(from, to, ec);
        if (ec) {
            // Windows rename-over-existing can fail — remove + retry
            // once (same pattern as ProjectStore::Save's tmp+rename).
            ec.clear();
            std::filesystem::remove(to, ec);
            ec.clear();
            std::filesystem::rename(from, to, ec);
            if (ec) {
                return Fail(error_out,
                            "autosave rotation failed for slot " +
                                std::to_string(n) + ": " + ec.message());
            }
        }
    }

    const std::string slot1 =
        AutosavePathFor(project_path_, autosave_dir_, 1);
    // Save failure keeps dirty_ set so the next due call retries.
    if (!ProjectStore::Save(project, slot1, error_out)) {
        return false;
    }
    dirty_ = false;
    last_save_ms_ = now_ms;
    has_saved_ = true;
    return true;
}

std::vector<std::string> AutosaveManager::ListAutosaves() const {
    return ListAutosaves(project_path_, autosave_dir_);
}

std::vector<std::string> AutosaveManager::ListAutosaves(
    const std::string& utf8_project_path,
    const std::string& utf8_autosave_dir) {
    std::vector<std::string> out;
    if (utf8_project_path.empty() || utf8_autosave_dir.empty()) {
        return out;
    }
    const std::string basename = BasenameFor(utf8_project_path);
    const std::filesystem::path dir = Path::FromUtf8(utf8_autosave_dir);

    std::vector<std::pair<int, std::string>> hits;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code tec;
        if (!it->is_regular_file(tec) || tec) {
            continue;
        }
        const int slot =
            SlotFromFilename(Path::ToUtf8(it->path().filename()), basename);
        if (slot >= 1) {
            hits.emplace_back(slot, Path::ToUtf8(it->path()));
        }
    }
    // Slot N=1 is the newest — ascending N is newest-first.
    std::sort(hits.begin(), hits.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    out.reserve(hits.size());
    for (auto& h : hits) {
        out.push_back(std::move(h.second));
    }
    return out;
}

bool AutosaveManager::LoadLatest(Project* project_out,
                                 std::string* error_out) const {
    return LoadLatest(project_path_, autosave_dir_, project_out, error_out);
}

bool AutosaveManager::LoadLatest(const std::string& utf8_project_path,
                                 const std::string& utf8_autosave_dir,
                                 Project* project_out,
                                 std::string* error_out) {
    if (project_out == nullptr) {
        return Fail(error_out, "autosave LoadLatest: project_out is required");
    }
    const std::vector<std::string> slots =
        ListAutosaves(utf8_project_path, utf8_autosave_dir);
    if (slots.empty()) {
        return Fail(error_out,
                    "autosave LoadLatest: no autosaves found for " +
                        utf8_project_path);
    }
    // Newest-first; a corrupt newest copy falls back to the next-oldest
    // slot rather than failing the recovery outright.
    std::string first_error;
    for (const std::string& slot : slots) {
        Project loaded;
        std::string lerr;
        if (ProjectStore::Load(slot, &loaded, &lerr)) {
            *project_out = std::move(loaded);
            return true;
        }
        if (first_error.empty()) {
            first_error = lerr;
        }
    }
    return Fail(error_out, "autosave LoadLatest: no readable autosave: " +
                               first_error);
}

}  // namespace XER::VideoEditor
