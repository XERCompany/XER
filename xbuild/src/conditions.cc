#include "conditions.h"
#include "expressions.h"
#include <json/json.h>
#include <memory>
#include <re2/re2.h>
namespace XBuild {
bool EvaluateCondition(const std::string& expression, const Environment& environment,
                       bool& result, std::string& error) {
    static const RE2 grammar(R"(^\s*(\$<GET_ENV:[A-Za-z_][A-Za-z0-9_]*>)\s*(==|!=)\s*("(?:[^"\\]|\\.)*")\s*$)");
    std::string left, operation, right;
    if (!RE2::FullMatch(expression, grammar, &left, &operation, &right)) {
        error = "Expected $<GET_ENV:NAME> == \"value\" or != in condition: " + expression;
        return false;
    }
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value value;
    if (!reader->parse(right.data(), right.data() + right.size(), &value, &error)) return false;
    const auto name = left.substr(10, left.size() - 11);
    std::string actual;
    environment.Get(name, actual);
    result = actual == value.asString();
    if (operation == "!=") result = !result;
    return true;
}
}
