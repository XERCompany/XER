#include "shell_prefs.h"

#include <fstream>
#include <sstream>

#include <json/json.h>

#include "helper/env_utils.h"
#include "helper/path_utils.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace Path = XER::Helper::Path;

Json::Value SizesToJson(const std::vector<float>& sizes) {
    Json::Value arr(Json::arrayValue);
    for (const float f : sizes) {
        arr.append(static_cast<double>(f));
    }
    return arr;
}

void SizesFromJson(const Json::Value& v, std::vector<float>* out) {
    if (!v.isArray()) return;
    out->clear();
    out->reserve(v.size());
    for (const auto& e : v) {
        if (e.isNumeric()) out->push_back(static_cast<float>(e.asDouble()));
    }
}

}  // namespace

std::filesystem::path DefaultShellPrefsPath() {
    // Same env chain as MediaCatalog::DefaultDbPath() (media_catalog.cc)
    // — kept local so the shell does not pull a DB include for a path.
    std::string base =
        Path::NormalizeUtf8Path(XER::Helper::GetEnv("LOCALAPPDATA"));
    if (base.empty()) {
        base = Path::NormalizeUtf8Path(XER::Helper::GetEnv("XDG_DATA_HOME"));
    }
    if (base.empty()) {
        const std::string home =
            Path::NormalizeUtf8Path(XER::Helper::GetEnv("HOME"));
        if (!home.empty()) base = home + "/.local/share";
    }
    if (base.empty()) {
        std::error_code ec;
        base = Path::ToUtf8(std::filesystem::temp_directory_path(ec));
    }
    if (base.empty()) base = ".";
    return Path::FromUtf8(base + "/XER/VideoEditor/ui2_shell.json");
}

bool LoadShellPrefs(const std::filesystem::path& path, ShellPrefs* out,
                    std::string* error) {
    if (out == nullptr) {
        if (error) *error = "null out";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + Path::ToUtf8(path);
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    Json::Value root;
    std::string errs;
    Json::CharReaderBuilder rb;
    const std::string text = buf.str();
    if (!Json::parseFromStream(rb, buf, &root, &errs) || !root.isObject()) {
        if (error) *error = errs.empty() ? "not a JSON object" : errs;
        return false;
    }
    if (root["workspace"].isString()) {
        out->workspace = root["workspace"].asString();
    }
    const auto get_bool = [&root](const char* key, bool* field) {
        if (root[key].isBool()) *field = root[key].asBool();
    };
    get_bool("left_visible", &out->left_visible);
    get_bool("right_visible", &out->right_visible);
    get_bool("status_visible", &out->status_visible);
    get_bool("source_monitor_visible", &out->source_monitor_visible);
    get_bool("monitors_tabbed", &out->monitors_tabbed);
    if (root["left_tab"].isInt()) out->left_tab = root["left_tab"].asInt();
    if (root["right_tab"].isInt()) {
        out->right_tab = root["right_tab"].asInt();
    }
    // ---- app-level prefs (optional keys — defaults survive) ---------
    get_bool("autosave_enabled", &out->autosave_enabled);
    if (root["autosave_interval_s"].isInt()) {
        out->autosave_interval_s = root["autosave_interval_s"].asInt();
    }
    if (root["scratch_dir"].isString()) {
        out->scratch_dir = root["scratch_dir"].asString();
    }
    if (root["audio_sample_rate"].isInt()) {
        out->audio_sample_rate = root["audio_sample_rate"].asInt();
    }
    if (root["transcript_max_minutes"].isNumeric()) {
        out->transcript_max_minutes =
            root["transcript_max_minutes"].asDouble();
    }
    SizesFromJson(root["top_sizes"], &out->top_sizes);
    SizesFromJson(root["outer_sizes"], &out->outer_sizes);
    SizesFromJson(root["monitor_sizes"], &out->monitor_sizes);
    (void)text;
    return true;
}

bool SaveShellPrefs(const std::filesystem::path& path,
                    const ShellPrefs& prefs, std::string* error) {
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    Json::Value root(Json::objectValue);
    root["workspace"] = prefs.workspace;
    root["left_visible"] = prefs.left_visible;
    root["right_visible"] = prefs.right_visible;
    root["status_visible"] = prefs.status_visible;
    root["source_monitor_visible"] = prefs.source_monitor_visible;
    root["monitors_tabbed"] = prefs.monitors_tabbed;
    root["left_tab"] = prefs.left_tab;
    root["right_tab"] = prefs.right_tab;
    root["autosave_enabled"] = prefs.autosave_enabled;
    root["autosave_interval_s"] = prefs.autosave_interval_s;
    root["scratch_dir"] = prefs.scratch_dir;
    root["audio_sample_rate"] = prefs.audio_sample_rate;
    root["transcript_max_minutes"] = prefs.transcript_max_minutes;
    root["top_sizes"] = SizesToJson(prefs.top_sizes);
    root["outer_sizes"] = SizesToJson(prefs.outer_sizes);
    root["monitor_sizes"] = SizesToJson(prefs.monitor_sizes);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    const std::string text = Json::writeString(wb, root);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + Path::ToUtf8(path);
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

}  // namespace XER::VideoEditor::Ui2
