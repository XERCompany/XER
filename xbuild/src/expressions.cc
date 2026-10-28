#include "expressions.h"
namespace XBuild {
bool Expand(const std::string& input, const Environment& environment, std::string& output, std::string& error) {
    std::string result;
    std::size_t cursor = 0;
    while (cursor < input.size()) {
        const auto begin = input.find("$<", cursor);
        if (begin == std::string::npos) { result += input.substr(cursor); break; }
        result += input.substr(cursor, begin - cursor);
        const auto end = input.find('>', begin + 2);
        if (end == std::string::npos) { error = "Unterminated environment expression: " + input; return false; }
        const auto expression = input.substr(begin + 2, end - begin - 2);
        if (!expression.starts_with("GET_ENV:") || expression.size() == 8) {
            error = "Unsupported expression: " + expression;
            return false;
        }
        std::string value;
        if (!environment.Get(expression.substr(8), value)) {
            error = "Environment variable is not defined: " + expression.substr(8);
            return false;
        }
        result += value;
        cursor = end + 1;
    }
    output = std::move(result);
    return true;
}
bool ExpandList(const std::vector<std::string>& input, const Environment& environment,
                std::vector<std::string>& output, std::string& error) {
    for (const auto& item : input) {
        std::string expanded;
        if (!Expand(item, environment, expanded, error)) return false;
        output.push_back(std::move(expanded));
    }
    return true;
}
}
