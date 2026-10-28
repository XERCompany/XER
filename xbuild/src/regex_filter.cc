#include "regex_filter.h"
namespace XBuild {
bool RegexFilter::Parse(const std::string& expression, std::string& error) {
    if (!expression.starts_with("<REGEX:/") || !expression.ends_with(">")) {
        error = "Expected <REGEX:/pattern/flags>: " + expression;
        return false;
    }
    const auto slash = expression.rfind('/');
    if (slash < 8) { error = "Missing closing slash in regex: " + expression; return false; }
    RE2::Options options;
    options.set_log_errors(false);
    const auto flags = expression.substr(slash + 1, expression.size() - slash - 2);
    for (char flag : flags) {
        if (flag == 'i') options.set_case_sensitive(false);
        else if (flag != 'g') { error = "Unsupported regex flag: " + std::string(1, flag); return false; }
    }
    regex_ = std::make_unique<RE2>(expression.substr(8, slash - 8), options);
    if (!regex_->ok()) { error = "Invalid RE2 expression: " + regex_->error(); return false; }
    return true;
}
bool RegexFilter::Matches(const std::string& name) const {
    return regex_ && RE2::PartialMatch(name, *regex_);
}
}
