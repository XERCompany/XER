#include "source_resolver.h"
#include "expressions.h"
#include "glob.h"
#include "path_utils.h"
#include <set>
namespace XBuild {
bool ResolveSources(const Project& project, const Json::Value& target, const Environment& environment,
                    std::vector<SourceUnit>& sources, std::string& error) {
    std::set<fs::path> compiled;
    for (const auto& entry : target["sources"]) {
        if (entry.isMember("async_sleep")) {
            SourceUnit unit;
            unit.delay = true;
            unit.delay_ms = entry["async_sleep"].asUInt();
            sources.push_back(std::move(unit));
            continue;
        }
        const auto patterns = entry.get("glob_aware_worker", false).asBool()
            ? Strings(entry["glob_input"]) : std::vector<std::string>{entry["input"].asString()};
        for (const auto& pattern : patterns) {
            std::string input;
            if (!Expand(pattern, environment, input, error)) return false;
            const bool header = entry.get("header", false).asBool() || input.starts_with("<HEADER:");
            if (input.starts_with("<HEADER:") && input.ends_with(">")) input = input.substr(8, input.size() - 9);
            std::vector<fs::path> matches;
            if (header) {
                const auto path = AbsolutePath(project.root, input);
                std::error_code ec;
                if (!fs::is_regular_file(path, ec)) { error = "Header not found: " + PathText(path); return false; }
                matches.push_back(path);
            } else if (!ExpandGlob(project, input, entry.get("glob_files", false).asBool() ||
                                  entry.get("glob_aware_worker", false).asBool(), matches, error)) return false;
            if (entry.isMember("out") && matches.size() != 1) {
                error = "An explicit object output requires exactly one source.";
                return false;
            }
            for (const auto& match : matches) {
                if (!header && !IsSourceFile(match)) { error = "Unsupported source extension: " + PathText(match); return false; }
                if (!header && !compiled.insert(match).second) { error = "Duplicate source: " + PathText(match); return false; }
                SourceUnit unit;
                unit.input = match;
                unit.header = header;
                unit.tool = entry.get("tool", project.tool).asString();
                unit.before = entry.get("start_post", "").asString();
                unit.after = entry.get("end_post", "").asString();
                if (entry.isMember("out")) {
                    std::string out;
                    if (!Expand(entry["out"].asString(), environment, out, error)) return false;
                    unit.output = AbsolutePath(project.root, out);
                }
                sources.push_back(std::move(unit));
            }
        }
    }
    return true;
}
}
