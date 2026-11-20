#pragma once

#include "config.h"
#include "xer/xer_value.h"

#include <absl/container/flat_hash_map.h>

#include <string>
#include <vector>

namespace Xer {

class XerLocal {
public:
    void Push();
    void Pop();

    void Set(std::string name, XerValue value);
    const XerValue* Get(const std::string& name) const;
    bool Contains(const std::string& name) const;

    std::size_t Depth() const { return frames_.size(); }

private:
    std::vector<absl::flat_hash_map<std::string, XerValue>> frames_;
};

}  // namespace Xer