#pragma once
#include "loader_base.h"

namespace XER::ML::Loaders {

/// CSV file loader: each row is a sample, last column used as label if has_labels=true
class LoaderCsv : public LoaderBase {
public:
    explicit LoaderCsv(bool has_labels = false, char delimiter = ',')
        : has_labels_(has_labels), delimiter_(delimiter) {}

    Dataset Load(const std::string& path) override;
    std::string Name() const override { return "CsvLoader"; }

private:
    bool has_labels_;
    char delimiter_;
};

}  // namespace XER::ML::Loaders
