#include "loader_stream.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

namespace XER::ML::Loaders {

void LoaderStream::Stream(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("LoaderStream: cannot open " + path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string cell;
        std::vector<float> row;
        while (std::getline(ss, cell, ','))
            try { row.push_back(std::stof(cell)); } catch (...) {}
        if (!row.empty()) callback_(std::move(row));
    }
}

}  // namespace XER::ML::Loaders
