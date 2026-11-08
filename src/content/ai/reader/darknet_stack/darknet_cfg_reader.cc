#include "darknet_cfg_reader.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace XER::ModelsBuilder::Reader::Darknet {

// --- CfgSection helpers ---

std::string CfgSection::Get(const std::string& key, const std::string& def) const {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : def;
}

int CfgSection::GetInt(const std::string& key, int def) const {
    auto it = params.find(key);
    if (it == params.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}

float CfgSection::GetFloat(const std::string& key, float def) const {
    auto it = params.find(key);
    if (it == params.end()) return def;
    try { return std::stof(it->second); } catch (...) { return def; }
}

// --- DarknetCfgReader ---

std::string DarknetCfgReader::Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool DarknetCfgReader::ParseFile(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return ParseString(ss.str());
}

bool DarknetCfgReader::ParseString(const std::string& content) {
    sections_.clear();
    CfgSection current;
    bool in_section = false;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            if (in_section) sections_.push_back(std::move(current));
            current = CfgSection{};
            current.name = line.substr(1, line.size() - 2);
            in_section = true;
        } else if (in_section) {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = Trim(line.substr(0, eq));
                std::string val = Trim(line.substr(eq + 1));
                // Strip inline comments
                auto hash = val.find('#');
                if (hash != std::string::npos) val = Trim(val.substr(0, hash));
                current.params[key] = val;
            }
        }
    }
    if (in_section) sections_.push_back(std::move(current));
    return true;
}

const CfgSection* DarknetCfgReader::NetSection() const {
    if (!sections_.empty()) return &sections_.front();
    return nullptr;
}

}  // namespace XER::ModelsBuilder::Reader::Darknet
