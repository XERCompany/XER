#pragma once

#include "config.h"
#include <string>

class XerScript {
public:
    explicit XerScript(std::string path);

    static bool Supports(const std::string& path);

    bool Run() const;
    bool Run(std::string* error_out) const;

private:
    std::string path_;
};