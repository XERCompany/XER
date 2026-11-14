#include "loader_yaml.h"

#include "helper/string_utils.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

// libxml2 requires -I/usr/include/libxml2 (set by CMake via pkg-config libxml-2.0)
#if __has_include(<libxml/parser.h>)
#  include <libxml/parser.h>
#  include <libxml/tree.h>
#  define LOADER_YAML_AVAILABLE 1
#endif

// yaml-cpp: real YAML datasets (.yaml/.yml). Expected shape:
//   dataset:
//     - [1.0, 2.0, 3.0]
//     - values: [4.0, 5.0, 6.0]
//       label: 1
#if __has_include(<yaml-cpp/yaml.h>)
#  include <yaml-cpp/yaml.h>
#  define LOADER_YAML_CPP_AVAILABLE 1
#endif

// XML dataset format expected by this loader:
// <dataset>
//   <row>1.0 2.0 3.0</row>
//   <row label="1">4.0 5.0 6.0</row>
// </dataset>

namespace XER::ML::Loaders {

#ifdef LOADER_YAML_CPP_AVAILABLE
static bool IsYamlPath(const std::string& path) {
    const std::string ext = XER::Helper::String::ToLowerAscii(
        std::filesystem::path(path).extension().string());
    return ext == ".yaml" || ext == ".yml";
}

static std::vector<float> NodeToFloats(const YAML::Node& node) {
    std::vector<float> vals;
    if (!node.IsSequence()) return vals;
    for (const auto& item : node) {
        if (item.IsScalar()) {
            vals.push_back(item.as<float>());
        }
    }
    return vals;
}

static Dataset LoadYamlCpp(const std::string& path) {
    const YAML::Node root = YAML::LoadFile(path);
    const YAML::Node dataset = root["dataset"];
    if (!dataset.IsSequence()) {
        throw std::runtime_error("YamlLoader: missing 'dataset' sequence in " + path);
    }

    Dataset ds;
    for (const auto& item : dataset) {
        if (item.IsSequence()) {
            ds.X.push_back(NodeToFloats(item));
            continue;
        }
        if (item.IsMap()) {
            const YAML::Node values = item["values"];
            const YAML::Node row = item["row"];
            const YAML::Node& data = values.IsDefined() ? values : row;
            auto floats = NodeToFloats(data);
            if (floats.empty()) continue;
            const YAML::Node label = item["label"];
            if (label.IsDefined() && label.IsScalar()) {
                ds.y.push_back(static_cast<int>(label.as<int>()));
            }
            ds.X.push_back(std::move(floats));
        }
    }
    return ds;
}
#endif

#ifdef LOADER_YAML_AVAILABLE
static std::vector<float> ParseNodeFloats(xmlNodePtr node) {
    std::vector<float> vals;
    xmlChar* content = xmlNodeGetContent(node);
    if (!content) return vals;
    std::istringstream ss(reinterpret_cast<const char*>(content));
    xmlFree(content);
    float v;
    while (ss >> v) vals.push_back(v);
    return vals;
}
#endif

Dataset LoaderYaml::Load(const std::string& path) {
#ifdef LOADER_YAML_CPP_AVAILABLE
    if (IsYamlPath(path)) {
        Dataset cached;
        if (TryLoadCachedDataset(Name(), path, &cached)) {
            return cached;
        }
        Dataset ds = LoadYamlCpp(path);
        StoreCachedDataset(Name(), path, ds);
        return ds;
    }
#endif

#ifdef LOADER_YAML_AVAILABLE
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    xmlDoc* doc = xmlReadFile(path.c_str(), nullptr,
                              XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc)
        throw std::runtime_error("YamlLoader: cannot parse XML file " + path);

    xmlNode* root = xmlDocGetRootElement(doc);
    Dataset ds;

    for (xmlNode* n = root ? root->children : nullptr; n; n = n->next) {
        if (n->type != XML_ELEMENT_NODE) continue;
        auto row = ParseNodeFloats(n);
        if (row.empty()) continue;

        xmlChar* lattr = xmlGetProp(n, reinterpret_cast<const xmlChar*>("label"));
        if (lattr) {
            ds.y.push_back(static_cast<int>(std::stoi(
                reinterpret_cast<const char*>(lattr))));
            xmlFree(lattr);
        }
        ds.X.push_back(std::move(row));
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();
    StoreCachedDataset(Name(), path, ds);
    return ds;
#else
    (void)path;
    return Dataset{};
#endif
}

}  // namespace XER::ML::Loaders
