// ztzvertfy.cc — integrity verifier for the XER TZ container.
//
//   ztzvertfy <archive.tz|.xexrj> [-r <member> ...] [-l]
//
// Validates magic/index bounds, every member's stored crc32, and the
// OVERFLOW manifest against the index. -r adds required-member checks
// (the same mechanism the .xexrj loader uses for manifest.toml
// declarations). -l lists members on success.
#include <cstdio>
#include <string>
#include <vector>

#include "tz/tz.h"

int main(int argc, char** argv) {
    std::string path;
    std::vector<std::string> required;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-r" && i + 1 < argc) {
            required.push_back(argv[++i]);
        } else if (a == "-l") {
            list = true;
        } else if (a == "-h" || a == "--help") {
            std::fprintf(stderr,
                "ztzvertfy — XER TZ integrity verifier\n"
                "  %s <archive.tz|.xexrj> [-r <member> ...] [-l]\n",
                argv[0]);
            return 0;
        } else if (!a.empty() && a.front() != '-' && path.empty()) {
            path = a;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            return 2;
        }
    }
    if (path.empty()) {
        std::fprintf(stderr, "ztzvertfy: archive path required\n");
        return 2;
    }

    const auto report = XER::Tz::TzVerify(path, required);
    std::printf("%s", XER::Tz::TzReportText(report).c_str());
    if (!report.ok) {
        return 1;
    }
    if (list) {
        std::string err;
        const auto arc = XER::Tz::TzArchive::Open(path, &err);
        if (arc) {
            for (const auto& e : arc->Entries()) {
                std::printf("%10llu  %08x  %s\n",
                            static_cast<unsigned long long>(e.size),
                            e.crc32, e.name.c_str());
            }
        }
    }
    return 0;
}
