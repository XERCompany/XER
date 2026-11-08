#include "loader_csv.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include <absl/strings/str_format.h>
#include <absl/strings/str_split.h>
#include <absl/strings/string_view.h>
#include <range/v3/view/all.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>

namespace XER::ML::Loaders {

Dataset LoaderCsv::Load(const std::string& path) {
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        const std::string msg = absl::StrFormat("CsvLoader: cannot open '%s'", path);
        LogError(msg);
        throw std::runtime_error(msg);
    }

    LogInfo(absl::StrFormat("CsvLoader: loading '%s'", path));

    Dataset ds;
    std::string line;
    while (std::getline(file, line)) {
        absl::string_view sv(line);
        if (sv.empty()) continue;

        // Split via absl; use range-v3 to filter empty tokens and convert to floats
        std::vector<absl::string_view> tokens =
            absl::StrSplit(sv, absl::ByChar(delimiter_), absl::SkipEmpty());

        if (tokens.empty()) continue;

        auto to_float = [](absl::string_view tok) -> float {
            try { return std::stof(std::string(tok)); }
            catch (...) { return 0.0f; }
        };

        auto row = ranges::to<std::vector<float>>(
            ranges::views::all(tokens) | ranges::views::transform(to_float));

        if (row.empty()) continue;
        if (has_labels_) {
            ds.y.push_back(static_cast<int>(row.back()));
            row.pop_back();
        }
        ds.X.push_back(std::move(row));
    }

    LogInfo(absl::StrFormat("CsvLoader: loaded %zu rows from '%s'",
                            ds.X.size(), path));
    StoreCachedDataset(Name(), path, ds);
    return ds;
}

}  // namespace XER::ML::Loaders
