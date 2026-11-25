#pragma once

// XER Content: open/save/directory dialogs ("opendialog").
//
// Native file-system pickers with Office/Adobe-style embedded widgets:
// the Windows implementation hosts the modern COM common item dialog
// (IFileOpenDialog / IFileSaveDialog) and injects caller-defined extra
// controls — a check box, a combo box and an edit field — into the
// dialog's explorer area through IFileDialogCustomize, with live
// control events wired through IFileDialogControlEvents. Their final
// values are surfaced in the PickResult alongside the chosen paths.
//
// Platform story: Windows-only for now. On Linux/macOS the same API
// compiles and every entry point returns a Result with `confirmed ==
// false` and `error` set — call sites can be written once and light up
// when a platform backend lands (GTK4/KDE portal, NSOpenPanel).
//
// The dialogs are modal and blocking: the *_Async wrappers run the
// blocking call on a worker thread and hand back std::future<Result>
// so UI code can keep its message loop alive.

#include <future>
#include <string>
#include <string_view>
#include <vector>

namespace XER::Content::OpenDialog {

// One file-type filter entry ("Images" -> "*.png;*.jpg").
struct Filter {
    std::string name;
    std::string spec;  // ';'-separated shell patterns, e.g. "*.png;*.jpg"
};

// Optional custom controls rendered INSIDE the system dialog via
// IFileDialogCustomize. Each control is opt-in: it is only added when
// its `enable_*` flag is set. The controls are grouped under a labeled
// visual group at the bottom of the dialog's explorer area, the same
// surface Office/Adobe extend with their own widgets.
//
// Live behaviour: the check box drives the edit field's enabled state
// through IFileDialogControlEvents (read-only pickers grey out the
// note box), demonstrating the event extension point.
struct CustomControls {
    // Check button (IDC-style: "Open read-only").
    bool enable_check = false;
    std::string check_label = "Open read-only";
    bool check_default = false;

    // Combo box ("Encoding:", "Format:").
    bool enable_combo = false;
    std::string combo_label = "Encoding:";
    std::vector<std::string> combo_items;
    int combo_default_index = 0;

    // Single-line edit box ("Note:").
    bool enable_edit = false;
    std::string edit_label = "Note:";
    std::string edit_default;
};

struct Options {
    // Owner window handle for modal parenting — pass a widget-kit
    // Window::GetHWND() (src/ui/core/window.h) as `void*`. nullptr shows
    // an unowned (taskbar-level) dialog.
    void* owner = nullptr;

    std::string title;

    // File-type filters; empty defaults to "All files (*.*)". Also see
    // ParseFilterSpec() for the compact "Name|*.a;*.b|Name2|*.c" form.
    std::vector<Filter> filters;

    std::string default_directory;  // initial folder (UTF-8)
    std::string default_name;       // pre-filled file name

    // ---- behaviour switches ----
    bool multi_select = false;     // open-file only: FOS_ALLOWMULTISELECT
    bool file_must_exist = true;   // open-file: FOS_FILEMUSTEXIST
    bool path_must_exist = true;   // FOS_PATHMUSTEXIST
    bool overwrite_prompt = true;  // save: FOS_OVERWRITEPROMPT
    bool create_prompt = true;     // directory-write: FOS_CREATEPROMPT

    // Save dialogs: extension auto-appended when the user omits it
    // (IFileSaveDialog::SetDefaultExtension), no leading dot ("png").
    std::string default_extension;

    CustomControls custom;
};

// Final values of the embedded custom controls, as they stood when the
// user pressed OK. All default/zero when no controls were enabled or a
// legacy fallback dialog ran (it cannot host extra widgets).
struct CustomResult {
    bool checked = false;
    int combo_index = -1;
    std::string combo_text;
    std::string edit_text;
};

struct Result {
    bool confirmed = false;          // user pressed OK (vs Cancel/close)
    std::vector<std::string> paths;  // UTF-8 absolute paths
    std::string error;               // non-empty on hard failure
    bool used_legacy_fallback = false;  // legacy comdlg32 path ran
    CustomResult custom;
};

// ---- pickers ------------------------------------------------------------
// All four take an Options (with an owner HWND seam for src/ui widget-kit
// windows) and return a Result. Modal + blocking; see the *_Async twins.

// "Open File" read mode — IFileOpenDialog (FOS_* must-exist flags,
// optional multi-select).
Result OpenFileRead(const Options& options);
// "Open File" write mode — IFileSaveDialog with overwrite prompt and
// extension auto-append from Options::default_extension.
Result OpenFileWrite(const Options& options);
// "Open Directory" read mode — IFileOpenDialog + FOS_PICKFOLDERS.
Result OpenDirectoryRead(const Options& options);
// "Open Directory" write mode — pick-or-create folder via
// FOS_PICKFOLDERS | FOS_CREATEPROMPT (the shell prompts to create a
// folder the user names but that does not exist yet).
Result OpenDirectoryWrite(const Options& options);

// Async twins: the modal dialog runs on a worker thread (COM is
// initialised there) so the calling UI thread stays responsive.
std::future<Result> OpenFileReadAsync(Options options);
std::future<Result> OpenFileWriteAsync(Options options);
std::future<Result> OpenDirectoryReadAsync(Options options);
std::future<Result> OpenDirectoryWriteAsync(Options options);

// ---- non-interactive helpers (CI-testable) -------------------------------

// Parses the compact filter syntax "Name|*.a;*.b|Name2|*.c" (the
// '|' separator matches the legacy lpstrFilter layout flattened to one
// string). Entries without a '|' pair are ignored.
std::vector<Filter> ParseFilterSpec(std::string_view spec);

// Builds the double-NUL-terminated lpstrFilter buffer used by
// OPENFILENAMEW ("Name\0spec\0...\0"). Exposed for self-tests.
std::wstring BuildLegacyFilterString(const std::vector<Filter>& filters);

// Validates an Options block without showing UI (e.g. combo_default_index
// inside combo_items range). Returns false and fills `error` on misuse.
bool ValidateOptions(const Options& options, std::string& error);

// Headless self-test: exercises the helpers above plus Result plumbing —
// never shows a dialog, safe for CI. Returns the number of failures and
// appends human-readable lines to `report`.
int RunSelftest(std::string& report);

}  // namespace XER::Content::OpenDialog
