// Smoke test for content/opendialog.
//
// Default run is fully headless (CI-safe): it calls
// OpenDialog::RunSelftest which exercises the non-interactive plumbing —
// filter-spec parsing, the legacy double-NUL filter buffer, option
// validation and Result zero-init — and never shows a dialog.
//
// Interactive verification is manual-only (shell dialogs cannot run in
// CI). Re-run the binary with one of:
//   --dlg-open        Open File read dialog (multi-select + widgets)
//   --dlg-save        Save File dialog (overwrite prompt + ext append)
//   --dlg-dir         Directory picker (read)
//   --dlg-dir-write   Directory pick-or-create (FOS_CREATEPROMPT)
// All interactive paths embed the custom controls (check box, combo,
// edit) so their rendering + value round-trip can be eyeballed.

#include <cstdio>
#include <cstring>
#include <string>

#include "content/opendialog/opendialog.h"

using XER::Content::OpenDialog::Options;
using XER::Content::OpenDialog::Result;

namespace {

void PrintResult(const char* tag, const Result& r) {
    std::printf("[%s] confirmed=%d legacy=%d error=\"%s\"\n", tag,
                r.confirmed ? 1 : 0, r.used_legacy_fallback ? 1 : 0,
                r.error.c_str());
    for (const std::string& p : r.paths)
        std::printf("  path: %s\n", p.c_str());
    std::printf(
        "  custom: checked=%d combo=%d(\"%s\") edit=\"%s\"\n",
        r.custom.checked ? 1 : 0, r.custom.combo_index,
        r.custom.combo_text.c_str(), r.custom.edit_text.c_str());
}

Options DemoOptions(const char* title) {
    Options o;
    o.title = title;
    o.filters = XER::Content::OpenDialog::ParseFilterSpec(
        "Images|*.png;*.jpg;*.bmp|All files|*.*");
    o.custom.enable_check = true;
    o.custom.check_label = "Open read-only";
    o.custom.enable_combo = true;
    o.custom.combo_label = "Encoding:";
    o.custom.combo_items = {"utf-8", "utf-16", "shift-jis"};
    o.custom.combo_default_index = 0;
    o.custom.enable_edit = true;
    o.custom.edit_label = "Note:";
    o.custom.edit_default = "picked from XER_OpenDialog_Smoke";
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        namespace OD = XER::Content::OpenDialog;
#ifdef _WIN32
        if (std::strcmp(argv[1], "--dlg-open") == 0) {
            Options o = DemoOptions("Open file (multi-select)");
            o.multi_select = true;
            PrintResult("dlg-open", OD::OpenFileRead(o));
            return 0;
        }
        if (std::strcmp(argv[1], "--dlg-save") == 0) {
            Options o = DemoOptions("Save file as");
            o.default_name = "untitled.png";
            o.default_extension = "png";
            PrintResult("dlg-save", OD::OpenFileWrite(o));
            return 0;
        }
        if (std::strcmp(argv[1], "--dlg-dir") == 0) {
            Options o = DemoOptions("Pick a directory");
            PrintResult("dlg-dir", OD::OpenDirectoryRead(o));
            return 0;
        }
        if (std::strcmp(argv[1], "--dlg-dir-write") == 0) {
            Options o = DemoOptions("Pick or create a directory");
            PrintResult("dlg-dir-write", OD::OpenDirectoryWrite(o));
            return 0;
        }
#else
        std::printf("interactive --dlg-* modes are Windows-only; the "
                    "module reports '%s' on this build\n",
                    OD::OpenFileRead(Options{}).error.c_str());
        return 0;
#endif
        std::fprintf(stderr, "unknown flag: %s\n", argv[1]);
        return 2;
    }

    std::string report;
    const int failures = XER::Content::OpenDialog::RunSelftest(report);
    std::fputs(report.c_str(), stdout);
    std::printf("opendialog selftest: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
