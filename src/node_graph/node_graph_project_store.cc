#include "node_graph/node_graph_project_store.h"

#include <chrono>
#include <filesystem>
#include <system_error>

#include "sql/content/value.h"
#include "sql/storage/sqlite_database.h"

namespace XER::NodeGraph {
namespace {

using XER::Engine::SQL::Content::Value;
using XER::Engine::SQL::Modern::Query;
using XER::Engine::SQL::Storage::Row;
using XER::Engine::SQL::Storage::SQLiteDatabase;

Value VStr(const std::string& s) { return Value{s}; }
Value VInt(std::int64_t v) { return Value{v}; }
Value VReal(double v) { return Value{v}; }

std::string AsString(const Value& v) {
    return std::holds_alternative<std::string>(v) ? std::get<std::string>(v)
                                                  : std::string{};
}
std::int64_t AsInt(const Value& v) {
    if (std::holds_alternative<std::int64_t>(v)) return std::get<std::int64_t>(v);
    if (std::holds_alternative<double>(v)) return static_cast<std::int64_t>(std::get<double>(v));
    return 0;
}
double AsReal(const Value& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    if (std::holds_alternative<std::int64_t>(v)) return static_cast<double>(std::get<std::int64_t>(v));
    return 0.0;
}

// NOTE: SQLiteDatabase::Execute prepares a single statement — a multi-
// statement script would silently run only the first CREATE TABLE. Keep
// every schema statement separate.
constexpr const char* kSchemaStatements[] = {
    R"sql(CREATE TABLE IF NOT EXISTS projects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    created_at REAL NOT NULL,
    updated_at REAL NOT NULL,
    version INTEGER NOT NULL DEFAULT 1,
    cache_path TEXT NOT NULL DEFAULT '',
    nodes_data TEXT NOT NULL DEFAULT '',
    icon TEXT NOT NULL DEFAULT ''
))sql",
    R"sql(CREATE TABLE IF NOT EXISTS graphs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    graph_json TEXT NOT NULL,
    created_at REAL NOT NULL,
    updated_at REAL NOT NULL,
    UNIQUE(project_id, name)
))sql",
    "CREATE INDEX IF NOT EXISTS idx_graphs_project ON graphs(project_id)",
    R"sql(CREATE TABLE IF NOT EXISTS presets (
    node_type TEXT NOT NULL,
    name TEXT NOT NULL,
    params_json TEXT NOT NULL,
    created_at REAL NOT NULL,
    updated_at REAL NOT NULL,
    PRIMARY KEY (node_type, name)
))sql",
};

}  // namespace

ProjectStore::ProjectStore(std::string db_path) : db_path_(std::move(db_path)) {}

ProjectStore::~ProjectStore() { Close(); }

double ProjectStore::NowSeconds() {
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool ProjectStore::Open(std::string* error_out) {
    Close();
    if (db_path_.empty()) {
        if (error_out) *error_out = "empty database path";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(db_path_).parent_path(), ec);
    db_ = SQLiteDatabase::Open(db_path_, error_out);
    if (db_ == nullptr) {
        return false;
    }
    return EnsureSchema(error_out);
}

void ProjectStore::Close() { db_.reset(); }

bool ProjectStore::EnsureSchema(std::string* error_out) {
    for (const char* sql : kSchemaStatements) {
        if (!db_->Execute(sql, {}, error_out)) return false;
    }
    // Columns added after the initial schema — ALTER fails harmlessly on
    // databases that already have them (duplicate column error ignored).
    db_->Execute("ALTER TABLE projects ADD COLUMN version INTEGER NOT NULL DEFAULT 1", {}, nullptr);
    db_->Execute("ALTER TABLE projects ADD COLUMN cache_path TEXT NOT NULL DEFAULT ''", {}, nullptr);
    db_->Execute("ALTER TABLE projects ADD COLUMN nodes_data TEXT NOT NULL DEFAULT ''", {}, nullptr);
    db_->Execute("ALTER TABLE projects ADD COLUMN icon TEXT NOT NULL DEFAULT ''", {}, nullptr);
    return true;
}

std::vector<ProjectStore::ProjectInfo> ProjectStore::ListProjects() {
    std::vector<ProjectInfo> out;
    if (!IsOpen()) return out;
    std::vector<Row> rows;
    if (!db_->Query(
            "SELECT p.id, p.name, p.updated_at,"
            " (SELECT COUNT(*) FROM graphs g WHERE g.project_id = p.id),"
            " p.version, p.cache_path, p.nodes_data, p.icon"
            " FROM projects p ORDER BY p.name",
            {}, rows, nullptr)) {
        return out;
    }
    for (const auto& r : rows) {
        if (r.columns.size() < 4) continue;
        ProjectInfo p;
        p.id = AsInt(r.columns[0]);
        p.name = AsString(r.columns[1]);
        p.updated_at = AsReal(r.columns[2]);
        p.graph_count = static_cast<int>(AsInt(r.columns[3]));
        if (r.columns.size() > 4) p.version = static_cast<int>(AsInt(r.columns[4]));
        if (r.columns.size() > 5) p.cache_path = AsString(r.columns[5]);
        if (r.columns.size() > 6) p.nodes_data = AsString(r.columns[6]);
        if (r.columns.size() > 7) p.icon = AsString(r.columns[7]);
        out.push_back(std::move(p));
    }
    return out;
}

std::int64_t ProjectStore::CreateProject(const std::string& name,
                                         std::string* error_out) {
    if (!IsOpen() || name.empty()) {
        if (error_out) *error_out = "store closed or empty name";
        return 0;
    }
    const double now = NowSeconds();
    if (!db_->Execute(
            "INSERT INTO projects(name, created_at, updated_at) VALUES(?, ?, ?)",
            {VStr(name), VReal(now), VReal(now)}, error_out)) {
        return 0;
    }
    return db_->LastInsertRowId();
}

bool ProjectStore::RenameProject(std::int64_t id, const std::string& name,
                                 std::string* error_out) {
    if (!IsOpen()) return false;
    return db_->Execute(
        "UPDATE projects SET name = ?, updated_at = ? WHERE id = ?",
        {VStr(name), VReal(NowSeconds()), VInt(id)}, error_out);
}

bool ProjectStore::DeleteProject(std::int64_t id, std::string* error_out) {
    if (!IsOpen()) return false;
    return db_->Execute("DELETE FROM projects WHERE id = ?", {VInt(id)}, error_out);
}

bool ProjectStore::GetProject(std::int64_t id, ProjectInfo* out,
                              std::string* error_out) {
    if (!IsOpen() || out == nullptr) {
        if (error_out) *error_out = "store closed";
        return false;
    }
    std::vector<ProjectInfo> all = ListProjects();
    for (const auto& p : all) {
        if (p.id == id) { *out = p; return true; }
    }
    if (error_out) *error_out = "project not found";
    return false;
}

bool ProjectStore::SaveProjectConfig(std::int64_t id, const std::string& name,
                                     int version, const std::string& cache_path,
                                     const std::string& nodes_data,
                                     const std::string& icon,
                                     std::string* error_out) {
    if (!IsOpen() || name.empty()) {
        if (error_out) *error_out = "store closed or empty name";
        return false;
    }
    return db_->Execute(
        "UPDATE projects SET name = ?, version = ?, cache_path = ?,"
        " nodes_data = ?, icon = ?, updated_at = ? WHERE id = ?",
        {VStr(name), VInt(version), VStr(cache_path), VStr(nodes_data),
         VStr(icon), VReal(NowSeconds()), VInt(id)},
        error_out);
}

bool ProjectStore::SaveGraph(std::int64_t project_id, const std::string& name,
                             const std::string& graph_json,
                             std::string* error_out) {
    if (!IsOpen() || project_id <= 0 || name.empty()) {
        if (error_out) *error_out = "store closed, invalid project or empty name";
        return false;
    }
    const double now = NowSeconds();
    return db_->Execute(
        "INSERT INTO graphs(project_id, name, graph_json, created_at, updated_at)"
        " VALUES(?, ?, ?, ?, ?)"
        " ON CONFLICT(project_id, name) DO UPDATE SET"
        " graph_json = excluded.graph_json, updated_at = excluded.updated_at",
        {VInt(project_id), VStr(name), VStr(graph_json), VReal(now), VReal(now)},
        error_out);
}

bool ProjectStore::LoadGraph(std::int64_t project_id, const std::string& name,
                             std::string* graph_json_out,
                             std::string* error_out) {
    if (!IsOpen() || graph_json_out == nullptr) {
        if (error_out) *error_out = "store closed";
        return false;
    }
    std::vector<Row> rows;
    if (!db_->Query(
            "SELECT graph_json FROM graphs WHERE project_id = ? AND name = ?",
            {VInt(project_id), VStr(name)}, rows, error_out)) {
        return false;
    }
    if (rows.empty() || rows.front().columns.empty()) {
        if (error_out) *error_out = "graph not found";
        return false;
    }
    *graph_json_out = AsString(rows.front().columns.front());
    return true;
}

bool ProjectStore::DeleteGraph(std::int64_t project_id, const std::string& name,
                               std::string* error_out) {
    if (!IsOpen()) return false;
    return db_->Execute(
        "DELETE FROM graphs WHERE project_id = ? AND name = ?",
        {VInt(project_id), VStr(name)}, error_out);
}

bool ProjectStore::RenameGraph(std::int64_t project_id,
                               const std::string& old_name,
                               const std::string& new_name,
                               std::string* error_out) {
    if (!IsOpen() || new_name.empty()) {
        if (error_out) *error_out = "store closed or empty name";
        return false;
    }
    // Verify the source graph exists so callers get a real error instead of
    // a silent no-op UPDATE.
    std::vector<Row> probe;
    if (!db_->Query(
            "SELECT 1 FROM graphs WHERE project_id = ? AND name = ?",
            {VInt(project_id), VStr(old_name)}, probe, error_out) ||
        probe.empty()) {
        if (error_out && error_out->empty()) *error_out = "graph not found";
        return false;
    }
    const double now = NowSeconds();
    // Rename the graph row, then touch the parent project's updated_at.
    if (!db_->Execute(
            "UPDATE graphs SET name = ?, updated_at = ?"
            " WHERE project_id = ? AND name = ?",
            {VStr(new_name), VReal(now), VInt(project_id), VStr(old_name)},
            error_out)) {
        return false;
    }
    db_->Execute(
        "UPDATE projects SET updated_at = ? WHERE id = ?",
        {VReal(now), VInt(project_id)}, nullptr);
    return true;
}

std::vector<ProjectStore::GraphInfo> ProjectStore::ListGraphs(std::int64_t project_id) {
    std::vector<GraphInfo> out;
    if (!IsOpen()) return out;
    std::vector<Row> rows;
    if (!db_->Query(
            "SELECT id, project_id, name, updated_at FROM graphs"
            " WHERE project_id = ? ORDER BY name",
            {VInt(project_id)}, rows, nullptr)) {
        return out;
    }
    for (const auto& r : rows) {
        if (r.columns.size() < 4) continue;
        GraphInfo g;
        g.id = AsInt(r.columns[0]);
        g.project_id = AsInt(r.columns[1]);
        g.name = AsString(r.columns[2]);
        g.updated_at = AsReal(r.columns[3]);
        out.push_back(std::move(g));
    }
    return out;
}

bool ProjectStore::SavePreset(const std::string& node_type, const std::string& name,
                              const std::string& params_json,
                              std::string* error_out) {
    if (!IsOpen() || node_type.empty() || name.empty()) {
        if (error_out) *error_out = "store closed or empty key";
        return false;
    }
    const double now = NowSeconds();
    return db_->Execute(
        "INSERT INTO presets(node_type, name, params_json, created_at, updated_at)"
        " VALUES(?, ?, ?, ?, ?)"
        " ON CONFLICT(node_type, name) DO UPDATE SET"
        " params_json = excluded.params_json, updated_at = excluded.updated_at",
        {VStr(node_type), VStr(name), VStr(params_json), VReal(now), VReal(now)},
        error_out);
}

bool ProjectStore::LoadPreset(const std::string& node_type, const std::string& name,
                              std::string* params_json_out,
                              std::string* error_out) {
    if (!IsOpen() || params_json_out == nullptr) {
        if (error_out) *error_out = "store closed";
        return false;
    }
    std::vector<Row> rows;
    if (!db_->Query(
            "SELECT params_json FROM presets WHERE node_type = ? AND name = ?",
            {VStr(node_type), VStr(name)}, rows, error_out)) {
        return false;
    }
    if (rows.empty() || rows.front().columns.empty()) {
        if (error_out) *error_out = "preset not found";
        return false;
    }
    *params_json_out = AsString(rows.front().columns.front());
    return true;
}

std::vector<std::pair<std::string, std::string>> ProjectStore::ListPresets(
    const std::string& node_type) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!IsOpen()) return out;
    std::vector<Row> rows;
    Query q;
    q.text = "SELECT name, params_json FROM presets";
    std::vector<Value> params;
    if (node_type.empty()) {
        q.text += " ORDER BY node_type, name";
    } else {
        q.text += " WHERE node_type = ? ORDER BY name";
        params.push_back(VStr(node_type));
    }
    q.parameters = params;
    if (!db_->Query(q, rows, nullptr)) {
        return out;
    }
    for (const auto& r : rows) {
        if (r.columns.size() < 2) continue;
        out.emplace_back(AsString(r.columns[0]), AsString(r.columns[1]));
    }
    return out;
}

bool ProjectStore::DeletePreset(const std::string& node_type, const std::string& name,
                                std::string* error_out) {
    if (!IsOpen()) return false;
    return db_->Execute(
        "DELETE FROM presets WHERE node_type = ? AND name = ?",
        {VStr(node_type), VStr(name)}, error_out);
}

}  // namespace XER::NodeGraph
