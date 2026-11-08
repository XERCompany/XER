#pragma once
#include "loader_base.h"

namespace XER::ML::Loaders {

/// XML-structured dataset loader via libxml2 (link with -lxml2).
/// Expected format:
///   <dataset>
///     <row>1.0 2.0 3.0</row>
///     <row label="1">4.0 5.0 6.0</row>
///   </dataset>
class LoaderYaml : public LoaderBase {
public:
    Dataset Load(const std::string& path) override;
    std::string Name() const override { return "YamlLoader"; }
};

}  // namespace XER::ML::Loaders
