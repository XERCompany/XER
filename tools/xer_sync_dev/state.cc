/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "state.h"

#include <fstream>
#include <sstream>

#include "common.h"

namespace xer_sync_dev {

bool SyncState::Load(const std::filesystem::path& path,
                     std::string* error) {
    path_ = path;
    root_ = Json::Value(Json::objectValue);
    root_["version"] = 1;
    root_["deps"] = Json::Value(Json::objectValue);

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return true;  // no state file yet — fine

    std::stringstream ss;
    ss << in.rdbuf();
    const std::string doc = ss.str();
    Json::Value parsed;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    if (!reader->parse(doc.data(), doc.data() + doc.size(), &parsed,
                       &errs)) {
        // A corrupt state file must not wedge the tool — start clean
        // but tell the caller.
        if (error) {
            *error = "SyncState: " + path.string() +
                     " failed to parse (" + errs +
                     ") — starting with a fresh state";
        }
        return true;
    }
    if (parsed.isObject()) {
        root_ = std::move(parsed);
        if (!root_["deps"].isObject()) {
            root_["deps"] = Json::Value(Json::objectValue);
        }
        if (!root_.isMember("version")) root_["version"] = 1;
    }
    return true;
}

bool SyncState::Save(std::string* error) const {
    std::error_code ec;
    if (!path_.parent_path().empty()) {
        fs::create_directories(path_.parent_path(), ec);
    }
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (error) *error = "SyncState: cannot write " + path_.string();
        return false;
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "    ";
    out << Json::writeString(wb, root_);
    return static_cast<bool>(out);
}

Json::Value& SyncState::Dep(const std::string& name) {
    return root_["deps"][name];
}

const Json::Value* SyncState::FindDep(const std::string& name) const {
    if (!root_["deps"].isMember(name)) return nullptr;
    return &root_["deps"][name];
}

void SyncState::RemoveDep(const std::string& name) {
    root_["deps"].removeMember(name);
}

std::string SyncState::GetString(const std::string& dep,
                                 const std::string& key) const {
    const Json::Value* rec = FindDep(dep);
    if (!rec || !(*rec).isMember(key) || !(*rec)[key].isString()) {
        return {};
    }
    return (*rec)[key].asString();
}

void SyncState::SetString(const std::string& dep, const std::string& key,
                          const std::string& value) {
    Dep(dep)[key] = value;
}

int SyncState::GetInt(const std::string& dep, const std::string& key,
                      int fallback) const {
    const Json::Value* rec = FindDep(dep);
    if (!rec || !(*rec).isMember(key) || !(*rec)[key].isInt()) {
        return fallback;
    }
    return (*rec)[key].asInt();
}

void SyncState::SetInt(const std::string& dep, const std::string& key,
                       int v) {
    Dep(dep)[key] = v;
}

std::string SyncState::last_auto_run() const {
    return root_.get("last_auto_run", "").asString();
}

void SyncState::set_last_auto_run(const std::string& iso) {
    root_["last_auto_run"] = iso;
}

}  // namespace xer_sync_dev
