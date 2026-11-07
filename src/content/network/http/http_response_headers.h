/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <string>

namespace XER::network::http {

class HttpResponseHeaders {
public:
    // Parse the header block (everything before the blank line).
    void Parse(const std::string& header_block,
               int* status_code,
               std::string* status_text);

    bool Has(const std::string& name) const;
    std::string Get(const std::string& name) const;
    void Set(const std::string& name, const std::string& value);
    const std::map<std::string, std::string>& All() const;
    int  ContentLength() const;

private:
    std::map<std::string, std::string> headers_;
};

}  // namespace XER::network::http
