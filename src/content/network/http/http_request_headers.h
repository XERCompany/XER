/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace XER::network::http {

class HttpRequestHeaders {
public:
    void Set(const std::string& name, const std::string& value);
    bool Has(const std::string& name) const;
    std::string Get(const std::string& name) const;
    void Remove(const std::string& name);
    void Clear();
    const std::map<std::string, std::string>& All() const;
    // Serialises all headers as "Name: Value\r\n" pairs.
    std::string Serialize() const;

private:
    std::map<std::string, std::string> headers_;
};

}  // namespace XER::network::http
