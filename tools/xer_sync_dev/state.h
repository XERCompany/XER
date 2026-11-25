/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — persistent state file.
 * Lives at <output_directory>/.xersync-state.json and tracks, per dep:
 * last update check, upstream HEAD/describe, the applied
 * `XER-<version>` string and pending-update counts, so `sync`/`auto`
 * can honor automatic_update_interval_days without re-hitting the
 * network on every run.
 */
#pragma once

#include <filesystem>
#include <string>

#include <json/json.h>

namespace xer_sync_dev {

class SyncState {
public:
    // Loads `path` if it exists; missing file yields an empty state.
    bool Load(const std::filesystem::path& path, std::string* error);
    bool Save(std::string* error) const;

    const std::filesystem::path& path() const { return path_; }

    // Per-dep record accessors (record is created on first Dep() call).
    Json::Value& Dep(const std::string& name);
    const Json::Value* FindDep(const std::string& name) const;
    void RemoveDep(const std::string& name);

    std::string GetString(const std::string& dep,
                          const std::string& key) const;
    void SetString(const std::string& dep, const std::string& key,
                   const std::string& value);
    int GetInt(const std::string& dep, const std::string& key,
               int fallback = 0) const;
    void SetInt(const std::string& dep, const std::string& key, int v);

    std::string last_auto_run() const;
    void set_last_auto_run(const std::string& iso);

private:
    std::filesystem::path path_;
    Json::Value root_{Json::objectValue};
};

}  // namespace xer_sync_dev
