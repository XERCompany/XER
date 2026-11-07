#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace XER::Engine::SQL::Storage {
class SQLiteDatabase;
}

namespace XER::NodeGraph {

// SQLite-backed project storage for node graphs (projects -> named graphs,
// plus reusable node presets). Built on src/sql (XER::Engine::SQL::Storage).
// The database file path is supplied by the owner — typically inside the
// cache root, e.g. CacheManager::Persistent().root() / "node_graph" /
// "projects.db".
class ProjectStore {
public:
    explicit ProjectStore(std::string db_path);
    ~ProjectStore();

    ProjectStore(const ProjectStore&) = delete;
    ProjectStore& operator=(const ProjectStore&) = delete;

    bool IsOpen() const { return db_ != nullptr; }
    // Filesystem path of the underlying database file (for sibling dirs
    // like the project-icon storage folder).
    const std::string& DbPath() const { return db_path_; }
    bool Open(std::string* error_out = nullptr);
    void Close();

    struct ProjectInfo {
        std::int64_t id = 0;
        std::string name;
        int graph_count = 0;
        double updated_at = 0.0;
        int version = 1;
        std::string cache_path;
        std::string nodes_data;
        // Avatar/icon key — a preset palette hex ("#rrggbb") or empty.
        std::string icon;
    };
    struct GraphInfo {
        std::int64_t id = 0;
        std::int64_t project_id = 0;
        std::string name;
        double updated_at = 0.0;
    };

    std::vector<ProjectInfo> ListProjects();
    std::int64_t CreateProject(const std::string& name, std::string* error_out = nullptr);
    bool RenameProject(std::int64_t id, const std::string& name, std::string* error_out = nullptr);
    bool DeleteProject(std::int64_t id, std::string* error_out = nullptr);
    // Fetches one project row (all config fields) by id.
    bool GetProject(std::int64_t id, ProjectInfo* out, std::string* error_out = nullptr);
    // Writes name + version + cache_path + nodes_data in one UPDATE.
    bool SaveProjectConfig(std::int64_t id, const std::string& name,
                           int version, const std::string& cache_path,
                           const std::string& nodes_data,
                           const std::string& icon,
                           std::string* error_out = nullptr);

    // Saves (inserts or replaces) a serialized graph under project/name.
    bool SaveGraph(std::int64_t project_id, const std::string& name,
                   const std::string& graph_json, std::string* error_out = nullptr);
    bool LoadGraph(std::int64_t project_id, const std::string& name,
                   std::string* graph_json_out, std::string* error_out = nullptr);
    bool DeleteGraph(std::int64_t project_id, const std::string& name,
                     std::string* error_out = nullptr);
    // Renames a graph inside a project (also bumps the project's updated_at).
    bool RenameGraph(std::int64_t project_id, const std::string& old_name,
                     const std::string& new_name,
                     std::string* error_out = nullptr);
    std::vector<GraphInfo> ListGraphs(std::int64_t project_id);

    // Reusable parameter presets keyed by node type + preset name.
    bool SavePreset(const std::string& node_type, const std::string& name,
                    const std::string& params_json, std::string* error_out = nullptr);
    bool LoadPreset(const std::string& node_type, const std::string& name,
                    std::string* params_json_out, std::string* error_out = nullptr);
    std::vector<std::pair<std::string, std::string>> ListPresets(
        const std::string& node_type);
    bool DeletePreset(const std::string& node_type, const std::string& name,
                      std::string* error_out = nullptr);

private:
    bool EnsureSchema(std::string* error_out);
    static double NowSeconds();

    std::string db_path_;
    std::unique_ptr<XER::Engine::SQL::Storage::SQLiteDatabase> db_;
};

}  // namespace XER::NodeGraph
