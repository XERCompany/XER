#include "target_graph.h"
#include <functional>
#include <map>
namespace XBuild {
bool OrderTargets(const Json::Value& targets, std::vector<std::size_t>& order, std::string& error) {
    std::map<std::string, std::size_t> names;
    for (Json::ArrayIndex i = 0; i < targets.size(); ++i) names[targets[i]["name"].asString()] = i;
    std::vector<int> state(targets.size(), 0);
    std::function<bool(std::size_t)> visit = [&](std::size_t index) {
        if (state[index] == 2) return true;
        const auto& target = targets[static_cast<Json::ArrayIndex>(index)];
        if (state[index] == 1) { error = "Target dependency cycle at " + target["name"].asString(); return false; }
        state[index] = 1;
        for (const auto& dependency : target["depends_on"]) {
            const auto found = names.find(dependency.asString());
            if (found == names.end()) { error = "Unknown target dependency: " + dependency.asString(); return false; }
            if (!visit(found->second)) return false;
        }
        state[index] = 2;
        order.push_back(index);
        return true;
    };
    for (std::size_t i = 0; i < targets.size(); ++i) if (!visit(i)) return false;
    return true;
}
}
