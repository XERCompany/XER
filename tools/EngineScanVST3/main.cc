// EngineScanVST3 — standalone VST3 module scanner.
//
// Loads every *.vst3 module found under the scan roots, reads factory
// metadata and writes a JSON report. Run this OUTSIDE the editor: plugin
// DllMain code executes during the scan, so it is kept off the UI/audio
// threads of the main application.
//
// Usage:
//   EngineScanVST3.exe [root ...] [--out report.json] [--pretty]
// With no roots the default search locations are used
// (%CommonProgramFiles%\VST3 and common vendor dirs).

#include <cstdio>
#include <string>
#include <vector>

#include "audio/plugin_wrappers/vst3_plugin_scanner.h"

namespace {

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char ch : s) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> roots;
    std::string out_path;
    bool pretty = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--pretty") {
            pretty = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: EngineScanVST3 [root ...] [--out file.json] [--pretty]\n");
            return 0;
        } else {
            roots.push_back(arg);
        }
    }
    if (roots.empty()) {
        roots = XER::Audio::Plugin::DefaultVst3SearchRoots();
    }

    std::fprintf(stderr, "EngineScanVST3: scanning %zu root(s)...\n", roots.size());
    for (const auto& r : roots) std::fprintf(stderr, "  %s\n", r.c_str());

    std::string error;
    auto plugins = XER::Audio::Plugin::ScanVst3Plugins(roots, &error);
    if (!error.empty()) {
        std::fprintf(stderr, "scan warning: %s\n", error.c_str());
    }
    std::fprintf(stderr, "EngineScanVST3: %zu plugin class(es) found\n",
                 plugins.size());

    // Build the JSON document.
    std::string json;
    json += "{\"generated_by\":\"EngineScanVST3\",\"roots\":[";
    for (size_t i = 0; i < roots.size(); ++i) {
        if (i) json += ",";
        json += "\"" + JsonEscape(roots[i]) + "\"";
    }
    json += "],\"plugins\":[";
    for (size_t i = 0; i < plugins.size(); ++i) {
        // Reuse PrintPlugin's formatting by emitting into a memory stream is
        // overkill — emit compact JSON directly here.
        const auto& p = plugins[i];
        if (i) json += pretty ? ",\n    " : ",";
        if (pretty && i == 0) json += "\n    ";
        json += "{\"path\":\"" + JsonEscape(p.path) +
                "\",\"module\":\"" + JsonEscape(p.module_path) +
                "\",\"name\":\"" + JsonEscape(p.name) +
                "\",\"vendor\":\"" + JsonEscape(p.vendor) +
                "\",\"category\":\"" + JsonEscape(p.category) +
                "\",\"version\":\"" + JsonEscape(p.version) + "\"}";
    }
    if (pretty && !plugins.empty()) json += "\n  ";
    json += "]}\n";

    FILE* out = stdout;
    if (!out_path.empty()) {
        out = std::fopen(out_path.c_str(), "wb");
        if (!out) {
            std::fprintf(stderr, "EngineScanVST3: cannot open %s\n",
                         out_path.c_str());
            return 1;
        }
    }
    std::fwrite(json.data(), 1, json.size(), out);
    if (out != stdout) std::fclose(out);
    return 0;
}
