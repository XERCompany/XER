/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "manifest.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include <json/json.h>

#include "common.h"

namespace xer_sync_dev {

bool LoadManifest(const std::filesystem::path& path, Manifest* out,
                  std::string* error) {
    auto fail = [error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (!out) return fail("LoadManifest: null out");

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return fail("LoadManifest: cannot open " + path.string());
    }

    // Strip `-- ...` / `// ...` comment lines (the spec file starts with
    // `-- format: json`, which is not valid JSON).
    std::stringstream cleaned;
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = Trim(line);
        if (StartsWith(t, "--") || StartsWith(t, "//")) continue;
        cleaned << line << '\n';
    }

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    const std::string doc = cleaned.str();
    if (!reader->parse(doc.data(), doc.data() + doc.size(), &root, &errs)) {
        return fail("LoadManifest: JSON parse error in " + path.string() +
                    ": " + errs);
    }
    if (!root.isObject()) {
        return fail("LoadManifest: root is not a JSON object");
    }

    Manifest m;
    m.automatic_update = root.get("automatic_update", false).asBool();
    m.automatic_update_interval_days =
        root.get("automatic_update_interval_days", 7).asInt();
    m.automatic_update_check_on_start =
        root.get("automatic_update_check_on_start", false).asBool();
    m.automatic_create_patch_for_cmake_all_dependencies =
        root.get("automatic_create_patch_for_cmake_all_dependencies",
                 false)
            .asBool();
    m.prefix_new_version_all_dependencies =
        root.get("prefix_new_version_all_dependencies", "XER-").asString();
    m.output_directory =
        root.get("output_directory", "third_party").asString();
    m.build_mode = root.get("build_mode", "static").asString();

    for (const auto& d : root["dependencies-from-github"]) {
        if (!d.isObject()) continue;
        Dependency dep;
        dep.kind = DepKind::kGit;
        dep.name = d.get("name", "").asString();
        dep.url = d.get("url", "").asString();
        dep.ref = d.get("ref", "").asString();
        dep.notes = d.get("notes", "").asString();
        dep.build_mode = d.get("build_mode", "").asString();
        dep.toolchain = d.get("toolchain", false).asBool();
        if (!dep.name.empty() && !dep.url.empty()) {
            m.deps.push_back(std::move(dep));
        }
    }
    for (const auto& d : root["dependencies-opensource"]) {
        if (!d.isObject()) continue;
        Dependency dep;
        dep.kind = DepKind::kBinary;
        dep.name = d.get("package_name", d.get("name", "").asString())
                       .asString();
        dep.url = d.get("url", "").asString();
        dep.notes = d.get("notes", "").asString();
        dep.build_mode = d.get("build_mode", "").asString();
        dep.toolchain = d.get("toolchain", false).asBool();
        for (const auto& mm : d["mirrors"]) {
            if (mm.isString()) dep.mirrors.push_back(mm.asString());
        }
        for (const auto& em : d["extract_map"]) {
            if (!em.isObject()) continue;
            ExtractMapEntry e;
            e.from = em.get("from", "").asString();
            e.to = em.get("to", "").asString();
            if (!e.from.empty() && !e.to.empty()) {
                dep.extract_map.push_back(std::move(e));
            }
        }
        if (!dep.name.empty() && !dep.url.empty()) {
            m.deps.push_back(std::move(dep));
        }
    }
    if (m.deps.empty()) {
        return fail("LoadManifest: no dependencies declared in " +
                    path.string());
    }

    std::error_code ec;
    m.manifest_path = fs::absolute(path, ec);
    if (ec) m.manifest_path = path;
    const fs::path manifest_dir = m.manifest_path.parent_path();

    // Resolution rule: output_directory is normally relative to the
    // manifest's directory — but when its basename equals the dir the
    // manifest itself lives in, the manifest sits inside its own
    // output dir (third_party/3RD.xersyunc -> output "third_party" IS
    // third_party/, not third_party/third_party).
    const fs::path out_rel(m.output_directory);
    if (!m.output_directory.empty() &&
        out_rel.filename() == manifest_dir.filename() &&
        out_rel.parent_path().empty()) {
        m.output_dir = manifest_dir;
    } else {
        m.output_dir = manifest_dir / m.output_directory;
    }
    const fs::path canon = fs::weakly_canonical(m.output_dir, ec);
    if (!ec) m.output_dir = canon;

    *out = std::move(m);
    return true;
}

std::vector<const Dependency*> DepsOfKind(const Manifest& m, DepKind k) {
    std::vector<const Dependency*> out;
    for (const auto& d : m.deps) {
        if (d.kind == k) out.push_back(&d);
    }
    return out;
}

const Dependency* FindDep(const Manifest& m, const std::string& name) {
    for (const auto& d : m.deps) {
        if (d.name == name) return &d;
    }
    return nullptr;
}

std::string EffectiveBuildMode(const Manifest& m, const Dependency& d) {
    return d.build_mode.empty() ? m.build_mode : d.build_mode;
}

namespace {

// Minimal JSON string escaping for values we write back (names/urls
// are user input and may contain quotes or backslashes).
std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Position of the ']' that closes the JSON array opening at `[`
// position `open` in `text`, honoring string literals/escapes.
// npos on unbalanced input.
size_t FindArrayClose(const std::string& text, size_t open) {
    int depth = 0;
    bool in_str = false, esc = false;
    for (size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '[') ++depth;
        else if (c == ']') {
            if (--depth == 0) return i;
        }
    }
    return std::string::npos;
}

// Renders one manifest entry object, indented like the existing file:
// `{` at 8 spaces, fields at 12.
std::string RenderEntry(const Dependency& dep) {
    std::string s = "        {\n";
    const std::string name_key =
        dep.kind == DepKind::kGit ? "name" : "package_name";
    s += "            \"" + name_key + "\": \"" + JsonEscape(dep.name) +
         "\",\n";
    s += "            \"url\": \"" + JsonEscape(dep.url) + "\"";
    if (!dep.ref.empty()) {
        s += ",\n            \"ref\": \"" + JsonEscape(dep.ref) + "\"";
    }
    if (!dep.build_mode.empty()) {
        s += ",\n            \"build_mode\": \"" +
             JsonEscape(dep.build_mode) + "\"";
    }
    if (!dep.notes.empty()) {
        s += ",\n            \"notes\": \"" + JsonEscape(dep.notes) + "\"";
    }
    if (!dep.mirrors.empty()) {
        s += ",\n            \"mirrors\": [\n";
        for (size_t i = 0; i < dep.mirrors.size(); ++i) {
            s += "                \"" + JsonEscape(dep.mirrors[i]) +
                 "\"" + (i + 1 < dep.mirrors.size() ? ",\n" : "\n");
        }
        s += "            ]";
    }
    if (!dep.extract_map.empty()) {
        s += ",\n            \"extract_map\": [\n";
        for (size_t i = 0; i < dep.extract_map.size(); ++i) {
            const auto& e = dep.extract_map[i];
            s += "                {\"from\": \"" + JsonEscape(e.from) +
                 "\", \"to\": \"" + JsonEscape(e.to) + "\"}" +
                 (i + 1 < dep.extract_map.size() ? ",\n" : "\n");
        }
        s += "            ]";
    }
    s += "\n        }";
    return s;
}

}  // namespace

bool AppendDepToManifest(const std::filesystem::path& path,
                         const Dependency& dep, std::string* error) {
    auto fail = [error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (dep.name.empty() || dep.url.empty()) {
        return fail("AppendDepToManifest: name and url are required");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return fail("AppendDepToManifest: cannot open " + path.string());
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());

    const char* key = dep.kind == DepKind::kGit
                          ? "\"dependencies-from-github\""
                          : "\"dependencies-opensource\"";
    const size_t key_pos = text.find(key);
    if (key_pos == std::string::npos) {
        // The array itself is absent — create it as the last member
        // of the root object.
        const size_t root_close =
            text.find_last_not_of(" \t\r\n");
        if (root_close == std::string::npos ||
            text[root_close] != '}') {
            return fail("AppendDepToManifest: root '}' not found in " +
                        path.string());
        }
        size_t last_member_end = root_close;
        while (last_member_end > 0 &&
               std::isspace(static_cast<unsigned char>(
                   text[last_member_end - 1]))) {
            --last_member_end;
        }
        const bool empty_obj =
            last_member_end > 0 && text[last_member_end - 1] == '{';

        std::string out = text;
        out.insert(root_close,
                   "    " + std::string(key) + ": [\n" +
                       RenderEntry(dep) + "\n    ]\n");
        out.insert(last_member_end, empty_obj ? "\n" : ",\n");
        std::ofstream fout(path, std::ios::binary | std::ios::trunc);
        if (!fout.is_open()) {
            return fail("AppendDepToManifest: cannot rewrite " +
                        path.string());
        }
        fout << out;
        if (!fout) {
            return fail("AppendDepToManifest: write failed for " +
                        path.string());
        }
        return true;
    }
    const size_t open = text.find('[', key_pos);
    if (open == std::string::npos) {
        return fail(std::string("AppendDepToManifest: no '[' after ") +
                    key);
    }
    const size_t close = FindArrayClose(text, open);
    if (close == std::string::npos) {
        return fail(std::string("AppendDepToManifest: unbalanced '[]' "
                                "for ") +
                    key);
    }

    // Walk back over whitespace before ']': a '}'/']'/'"' means the
    // array is non-empty and the previous element needs a comma.
    size_t last_elem_end = close;
    while (last_elem_end > open &&
           std::isspace(static_cast<unsigned char>(
               text[last_elem_end - 1]))) {
        --last_elem_end;
    }
    const bool empty_array =
        last_elem_end <= open + 1 || text[last_elem_end - 1] == '[';

    std::string out = text;
    const std::string entry = RenderEntry(dep);
    if (empty_array) {
        // ...": []" -> ...": [\n<entry>\n    ]"
        out.replace(close, 0, "\n" + entry + "\n    ");
    } else {
        // <prev-elem>\n    ] -> <prev-elem>,\n<entry>\n    ]
        out.replace(close, 0, entry + "\n    ");
        out.replace(last_elem_end, 0, ",\n");
    }

    std::ofstream fout(path, std::ios::binary | std::ios::trunc);
    if (!fout.is_open()) {
        return fail("AppendDepToManifest: cannot rewrite " +
                    path.string());
    }
    fout << out;
    if (!fout) {
        return fail("AppendDepToManifest: write failed for " +
                    path.string());
    }
    return true;
}

std::string DefaultNameFromUrl(const std::string& url, DepKind kind) {
    std::string u = url;
    while (!u.empty() && (u.back() == '/' || u.back() == '\\')) {
        u.pop_back();
    }
    const size_t q = u.find_first_of("?#");
    if (q != std::string::npos) u.resize(q);
    std::string base = u.substr(u.find_last_of("/\\") + 1);
    if (base.empty()) return {};

    if (kind == DepKind::kGit) {
        if (EndsWith(base, ".git")) base.resize(base.size() - 4);
        return base;
    }
    // opensource: strip a known archive/package extension.
    for (const char* ext :
         {".tar.bz2", ".tar.gz", ".tar.xz", ".tar.zst", ".nupkg",
          ".zip", ".tgz", ".txz", ".tbz2", ".7z", ".tar"}) {
        if (EndsWith(base, ext)) {
            base.resize(base.size() - std::strlen(ext));
            break;
        }
    }
    // A bare version tail ("name.1.32.0" -> "name") keeps
    // `microsoft.ml.onnxruntime` sane; plain tarballs are untouched.
    if (base.size() > 8) {
        const size_t first_digit = base.find_first_of("0123456789");
        if (first_digit != std::string::npos && first_digit > 0 &&
            base[first_digit - 1] == '.') {
            bool all_version = true;
            for (size_t i = first_digit; i < base.size(); ++i) {
                const char c = base[i];
                if (!std::isdigit(static_cast<unsigned char>(c)) &&
                    c != '.' && c != '-' && c != '_') {
                    all_version = false;
                    break;
                }
            }
            if (all_version) base.resize(first_digit - 1);
        }
    }
    return base;
}

}  // namespace xer_sync_dev
