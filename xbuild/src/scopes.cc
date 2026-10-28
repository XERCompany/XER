#include "scopes.h"
#include "conditions.h"
#include "expressions.h"
#include <json/json.h>
#include <memory>
#include <re2/re2.h>
namespace XBuild {
bool EvaluateScopes(const Json::Value& scopes, Environment& environment,
                    std::vector<std::string>& messages, std::string& error) {
    for (const auto& scope : scopes) {
        for (const auto& entry : scope["system"]) {
            const auto& branch = entry["if"];
            bool selected = false;
            if (!EvaluateCondition(branch["IN"].asString(), environment, selected, error)) return false;
            const auto action = branch.get(selected ? "TRUE" : "ELSE", "").asString();
            if (action.empty()) continue;
            if (action.starts_with("<LOCAL_SET_ENV:") && action.ends_with(">")) {
                const auto assignment = action.substr(15, action.size() - 16);
                const auto equal = assignment.find('=');
                static const RE2 name("[A-Za-z_][A-Za-z0-9_]*");
                if (equal == std::string::npos || !RE2::FullMatch(assignment.substr(0, equal), name)) {
                    error = "Invalid LOCAL_SET_ENV action: " + action;
                    return false;
                }
                std::string value;
                if (!Expand(assignment.substr(equal + 1), environment, value, error)) return false;
                environment.Set(assignment.substr(0, equal), value);
            } else if (action.starts_with("<LOG:") && action.ends_with(">")) {
                const auto text = action.substr(5, action.size() - 6);
                Json::CharReaderBuilder builder;
                std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                Json::Value value;
                if (!reader->parse(text.data(), text.data() + text.size(), &value, &error) || !value.isString()) {
                    error = "LOG requires a quoted JSON string: " + action;
                    return false;
                }
                std::string expanded;
                if (!Expand(value.asString(), environment, expanded, error)) return false;
                messages.push_back(std::move(expanded));
            } else {
                error = "Unsupported scope action: " + action;
                return false;
            }
        }
    }
    return true;
}
}
