#include "loader_yaml.h"

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

// XML dataset format expected by this loader:
// <dataset>
//   <row>1.0 2.0 3.0</row>
//   <row label="1">4.0 5.0 6.0</row>
// </dataset>

namespace XER::ML::Loaders {

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
