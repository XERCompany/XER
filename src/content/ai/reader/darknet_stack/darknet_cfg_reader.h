#pragma once

#include <map>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Darknet {

/// Represents one [section] from a Darknet .cfg file.
struct CfgSection {
    std::string name;                             ///< e.g. "net", "convolutional"
    std::map<std::string, std::string> params;    ///< key=value pairs

    /// Get param as string (returns def if absent)
    std::string Get(const std::string& key, const std::string& def = "") const;
    /// Get param as int
    int GetInt(const std::string& key, int def = 0) const;
    /// Get param as float
    float GetFloat(const std::string& key, float def = 0.0f) const;
};

/// Parses a Darknet-format .cfg text file into sections.
class DarknetCfgReader {
public:
    /// Parse from file path
    bool ParseFile(const std::string& filepath);

    /// Parse from string content
    bool ParseString(const std::string& content);

    const std::vector<CfgSection>& Sections() const { return sections_; }

    /// Convenience: return the [net] section (first section)
    const CfgSection* NetSection() const;

private:
    std::vector<CfgSection> sections_;

    static std::string Trim(const std::string& s);
};

}  // namespace XER::ModelsBuilder::Reader::Darknet
