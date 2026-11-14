#include "loader_json.h"

#include <fstream>
#include <stdexcept>

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

#if __has_include(<jsoncpp/json/json.h>)
#  include <jsoncpp/json/json.h>
#  define LOADER_JSON_AVAILABLE 1
#elif __has_include(<json/json.h>)
#  include <json/json.h>
#  define LOADER_JSON_AVAILABLE 1
#endif

// nlohmann_json fallback when jsoncpp is unavailable.
#if !defined(LOADER_JSON_AVAILABLE) && __has_include(<nlohmann/json.hpp>)
#  include <nlohmann/json.hpp>
#  define LOADER_JSON_NLOHMANN 1
#endif

namespace XER::ML::Loaders {

Dataset LoaderJson::Load(const std::string& path) {
#ifdef LOADER_JSON_AVAILABLE
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("JsonLoader: cannot open " + path);

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, f, &root, &errs))
        throw std::runtime_error("JsonLoader: parse error in " + path + ": " + errs);

    Dataset ds;

    // Format A: {"X": [[...], ...], "y": [...]}
    if (root.isObject() && root.isMember("X")) {
        for (const auto& row : root["X"]) {
            std::vector<float> r;
            for (const auto& v : row) r.push_back(v.asFloat());
            ds.X.push_back(std::move(r));
        }
        if (root.isMember("y"))
            for (const auto& v : root["y"]) ds.y.push_back(v.asInt());
        StoreCachedDataset(Name(), path, ds);
        return ds;
    }

    // Format B: [[f0, f1, ...], ...] with optional last-column label
    if (root.isArray()) {
        for (const auto& row : root) {
            if (!row.isArray()) continue;
            std::vector<float> r;
            for (const auto& v : row) r.push_back(v.asFloat());
            if (has_labels_ && !r.empty()) {
                ds.y.push_back(static_cast<int>(r.back()));
                r.pop_back();
            }
            ds.X.push_back(std::move(r));
        }
    }
    StoreCachedDataset(Name(), path, ds);
    return ds;
#elif defined(LOADER_JSON_NLOHMANN)
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("JsonLoader: cannot open " + path);

    const auto root = nlohmann::json::parse(f, nullptr, false);
    if (root.is_discarded())
        throw std::runtime_error("JsonLoader: parse error in " + path);

    Dataset ds;

    // Format A: {"X": [[...], ...], "y": [...]}
    if (root.is_object() && root.contains("X")) {
        for (const auto& row : root["X"]) {
            std::vector<float> r;
            for (const auto& v : row) r.push_back(v.get<float>());
            ds.X.push_back(std::move(r));
        }
        if (root.contains("y"))
            for (const auto& v : root["y"]) ds.y.push_back(v.get<int>());
        StoreCachedDataset(Name(), path, ds);
        return ds;
    }

    // Format B: [[f0, f1, ...], ...] with optional last-column label
    if (root.is_array()) {
        for (const auto& row : root) {
            if (!row.is_array()) continue;
            std::vector<float> r;
            for (const auto& v : row) r.push_back(v.get<float>());
            if (has_labels_ && !r.empty()) {
                ds.y.push_back(static_cast<int>(r.back()));
                r.pop_back();
            }
            ds.X.push_back(std::move(r));
        }
    }
    StoreCachedDataset(Name(), path, ds);
    return ds;
#else
    (void)path;
    return Dataset{};
#endif
}

}  // namespace XER::ML::Loaders
