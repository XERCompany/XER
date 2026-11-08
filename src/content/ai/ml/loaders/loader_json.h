#pragma once
#include "loader_base.h"

namespace XER::ML::Loaders {

/// Minimal JSON dataset loader (expects flat array-of-arrays format)
/// Format: [[f0,f1,...,label], ...] or {"X":[[...]], "y":[...]}
class LoaderJson : public LoaderBase {
public:
    explicit LoaderJson(bool has_labels = false) : has_labels_(has_labels) {}
    Dataset Load(const std::string& path) override;
    std::string Name() const override { return "JsonLoader"; }
private:
    bool has_labels_;
};

}  // namespace XER::ML::Loaders
