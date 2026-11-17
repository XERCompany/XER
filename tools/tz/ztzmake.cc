// ztzmake.cc — packer for the XER TZ container.
//
//   ztzmake <input_dir> -o <output.tz|.xexrj>
//
// Packs every file under input_dir (recursively) into a TZ archive.
// A fresh OVERFLOW integrity manifest is generated automatically —
// members are stored uncompressed and ordered by name so output is
// deterministic. Pair with ztzvertfy.exe to validate the result.
#include <cstdio>
#include <filesystem>
#include <string>

#include "tz/tz.h"

namespace {

void Usage(const char* argv0) {
    std::fprintf(stderr,
        "ztzmake — XER TZ packer\n"
        "  %s <input_dir> -o <output.tz|.xexrj>\n"
        "Options:\n"
        "  -o <path>   output archive path (required)\n"
        "  -v          list every packed member\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string input_dir, out_path;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "-v") {
            verbose = true;
        } else if (a == "-h" || a == "--help") {
            Usage(argv[0]);
            return 0;
        } else if (!a.empty() && a.front() != '-' &&
                   input_dir.empty()) {
            input_dir = a;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            Usage(argv[0]);
            return 2;
        }
    }
    if (input_dir.empty() || out_path.empty()) {
        Usage(argv[0]);
        return 2;
    }

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(out_path).parent_path(), ec);

    std::string error;
    if (!XER::Tz::TzWriteDir(input_dir, out_path, &error)) {
        std::fprintf(stderr, "ztzmake: %s\n", error.c_str());
        return 1;
    }

    // Verify what was just written — a packer that produces archives it
    // cannot re-open is worse than no packer.
    const auto report = XER::Tz::TzVerify(out_path);
    if (!report.ok) {
        std::fprintf(stderr, "ztzmake: self-verify failed:\n%s",
                     XER::Tz::TzReportText(report).c_str());
        return 1;
    }
    if (verbose) {
        std::string err2;
        const auto arc = XER::Tz::TzArchive::Open(out_path, &err2);
        if (arc) {
            for (const auto& e : arc->Entries()) {
                std::printf("%10llu  %s\n",
                            static_cast<unsigned long long>(e.size),
                            e.name.c_str());
            }
        }
    }
    std::printf("ztzmake: %s -> %s (%zu entries, %llu bytes)\n",
                input_dir.c_str(), out_path.c_str(), report.entries,
                static_cast<unsigned long long>(report.total_bytes));
    return 0;
}
