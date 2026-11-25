/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "manifest.h"

#include <fstream>
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

    for (const auto& d : root["dependencies-from-github"]) {
        if (!d.isObject()) continue;
        Dependency dep;
        dep.kind = DepKind::kGit;
        dep.name = d.get("name", "").asString();
        dep.url = d.get("url", "").asString();
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

}  // namespace xer_sync_dev
