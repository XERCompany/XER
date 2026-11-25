// XER Content: open/save/directory dialogs — Windows implementation.
//
// Primary path: the Vista+ common item dialogs (CLSID_FileOpenDialog /
// CLSID_FileSaveDialog) — required for the embedded custom controls
// (IFileDialogCustomize cannot extend the legacy comdlg32 pickers).
//
// Fallback path: GetOpenFileNameW / GetSaveFileNameW / SHBrowseForFolderW
// — taken automatically only when the COM dialog cannot be created
// (e.g. a runtime without the shell dialog CLSIDs); the custom widgets
// are silently dropped there and Result::used_legacy_fallback tells the
// caller the CustomResult fields carry no data. Precedent for the legacy
// calls exists in tools/video_editor/ui2/ve_app.cc and
// tools/ide_xejso/ide_app.cc.

#include "opendialog.h"

#include <cstdio>
#include <utility>

#ifdef _WIN32

#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "helper/string_utils.h"

using Microsoft::WRL::ComPtr;

namespace XER::Content::OpenDialog {
namespace {

// Control IDs inside the dialog's custom visual group.
constexpr DWORD kControlGroupId = 100;
constexpr DWORD kCheckId = 101;
constexpr DWORD kComboLabelId = 102;
constexpr DWORD kComboId = 103;
constexpr DWORD kEditLabelId = 104;
constexpr DWORD kEditId = 105;

// Per-call-thread COM init for the shell dialogs. S_OK/S_FALSE means we
// joined (or already owned) an STA and must uninit on scope exit;
// RPC_E_CHANGED_MODE means the thread is already MTA — the shell item
// dialogs tolerate that on modern Windows, so we proceed without owning
// an init.
class ScopedComInit {
public:
    ScopedComInit() {
        const HRESULT hr = CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        owned_ = SUCCEEDED(hr);
    }
    ~ScopedComInit() {
        if (owned_) CoUninitialize();
    }
    ScopedComInit(const ScopedComInit&) = delete;
    ScopedComInit& operator=(const ScopedComInit&) = delete;

private:
    bool owned_ = false;
};

// Dialog event sink. The shell dialog advises an IFileDialogEvents and
// QIs it for IFileDialogControlEvents to deliver control notifications,
// so both interfaces live on one object. Demonstrates the event
// extension point: toggling the "read-only" check box enables/disables
// the note edit box live inside the running dialog.
class DialogControlEvents final : public IFileDialogEvents,
                                  public IFileDialogControlEvents {
public:
    DialogControlEvents(IFileDialogCustomize* customize, DWORD check_id,
                        DWORD edit_id)
        : customize_(customize), check_id_(check_id), edit_id_(edit_id) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
            *ppv = static_cast<IFileDialogEvents*>(this);
        } else if (riid == IID_IFileDialogControlEvents) {
            *ppv = static_cast<IFileDialogControlEvents*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    // IFileDialogEvents — E_NOTIMPL requests default processing.
    STDMETHODIMP OnFileOk(IFileDialog*) override { return E_NOTIMPL; }
    STDMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP OnFolderChange(IFileDialog*) override { return E_NOTIMPL; }
    STDMETHODIMP OnSelectionChange(IFileDialog*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP OnShareViolation(IFileDialog*, IShellItem*,
                                  FDE_SHAREVIOLATION_RESPONSE*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP OnTypeChange(IFileDialog*) override { return E_NOTIMPL; }
    STDMETHODIMP OnOverwrite(IFileDialog*, IShellItem*,
                             FDE_OVERWRITE_RESPONSE*) override {
        return E_NOTIMPL;
    }

    // IFileDialogControlEvents
    STDMETHODIMP OnItemSelected(IFileDialogCustomize*, DWORD,
                                DWORD) override {
        return S_OK;
    }
    STDMETHODIMP OnButtonClicked(IFileDialogCustomize*, DWORD) override {
        return S_OK;
    }
    STDMETHODIMP OnCheckButtonToggled(IFileDialogCustomize*, DWORD id,
                                      BOOL checked) override {
        if (id == check_id_ && customize_ && edit_id_ != 0) {
            // Read-only pickers can't annotate — grey out the note box.
            customize_->SetControlState(
                edit_id_, checked ? CDCS_INACTIVE : CDCS_ENABLEDVISIBLE);
        }
        return S_OK;
    }
    STDMETHODIMP OnControlActivating(IFileDialogCustomize*, DWORD) override {
        return S_OK;
    }

private:
    ~DialogControlEvents() = default;
    IFileDialogCustomize* customize_;  // borrowed — owned by the dialog
    DWORD check_id_;
    DWORD edit_id_;
    std::atomic<ULONG> refs_{1};
};

std::wstring ToWide(std::string_view utf8) {
    return XER::Helper::String::Utf8ToWide(utf8);
}
std::string ToUtf8(std::wstring_view wide) {
    return XER::Helper::String::WideToUtf8(wide);
}

std::vector<Filter> EffectiveFilters(const Options& options) {
    if (!options.filters.empty()) return options.filters;
    return {Filter{"All files", "*.*"}};
}

// Builds the wide filter pair arrays + COMDLG_FILTERSPEC table. The
// `name_w`/`spec_w` vectors must outlive the dialog Show().
void BuildFilterTable(const std::vector<Filter>& filters,
                      std::vector<std::wstring>& name_w,
                      std::vector<std::wstring>& spec_w,
                      std::vector<COMDLG_FILTERSPEC>& table) {
    name_w.reserve(filters.size());
    spec_w.reserve(filters.size());
    for (const Filter& f : filters) {
        name_w.push_back(ToWide(f.name));
        spec_w.push_back(ToWide(f.spec));
    }
    table.reserve(filters.size());
    for (size_t i = 0; i < filters.size(); ++i)
        table.push_back({name_w[i].c_str(), spec_w[i].c_str()});
}

// Applies the common IFileDialog surface: flags, title, filters,
// defaults. `filters_w` must stay alive until Show() returns.
HRESULT ConfigureDialog(IFileDialog* dialog, const Options& options,
                        DWORD extra_flags,
                        const std::vector<COMDLG_FILTERSPEC>& filters_w) {
    DWORD flags = 0;
    if (FAILED(dialog->GetOptions(&flags))) return E_FAIL;
    flags |= extra_flags;
    if (options.path_must_exist) flags |= FOS_PATHMUSTEXIST;
    if (FAILED(dialog->SetOptions(flags))) return E_FAIL;

    if (!options.title.empty())
        dialog->SetTitle(ToWide(options.title).c_str());

    if (!filters_w.empty())
        dialog->SetFileTypes(static_cast<UINT>(filters_w.size()),
                             filters_w.data());

    if (!options.default_name.empty())
        dialog->SetFileName(ToWide(options.default_name).c_str());

    if (!options.default_extension.empty())
        dialog->SetDefaultExtension(
            ToWide(options.default_extension).c_str());

    if (!options.default_directory.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(
                ToWide(options.default_directory).c_str(), nullptr,
                IID_PPV_ARGS(&folder)))) {
            // SetFolder navigates the dialog into this directory.
            dialog->SetFolder(folder.Get());
        }
    }
    return S_OK;
}

// Adds the enabled CustomControls into a labelled visual group and
// advises the event sink. `sink_out`/`cookie_out` are set only when
// advise succeeded — the caller Unadvises after Show().
HRESULT EmbedCustomControls(IFileDialog* dialog, const Options& options,
                            ComPtr<DialogControlEvents>& sink_out,
                            DWORD& cookie_out) {
    const CustomControls& cc = options.custom;
    if (!cc.enable_check && !cc.enable_combo && !cc.enable_edit)
        return S_OK;

    ComPtr<IFileDialogCustomize> customize;
    HRESULT hr = dialog->QueryInterface(IID_PPV_ARGS(&customize));
    if (FAILED(hr) || !customize) return E_FAIL;

    customize->StartVisualGroup(kControlGroupId, L"XER options");

    if (cc.enable_check) {
        customize->AddCheckButton(kCheckId, ToWide(cc.check_label).c_str(),
                                  cc.check_default ? TRUE : FALSE);
    }
    if (cc.enable_combo && !cc.combo_items.empty()) {
        customize->AddText(kComboLabelId, ToWide(cc.combo_label).c_str());
        customize->AddComboBox(kComboId);
        for (size_t i = 0; i < cc.combo_items.size(); ++i) {
            customize->AddControlItem(
                kComboId, static_cast<DWORD>(i),
                ToWide(cc.combo_items[i]).c_str());
        }
        const DWORD def =
            (cc.combo_default_index >= 0 &&
             cc.combo_default_index <
                 static_cast<int>(cc.combo_items.size()))
                ? static_cast<DWORD>(cc.combo_default_index)
                : 0;
        customize->SetSelectedControlItem(kComboId, def);
    }
    if (cc.enable_edit) {
        customize->AddText(kEditLabelId, ToWide(cc.edit_label).c_str());
        customize->AddEditBox(kEditId, ToWide(cc.edit_default).c_str());
        // Sync the initial enabled state with the check box.
        if (cc.enable_check && cc.check_default)
            customize->SetControlState(kEditId, CDCS_INACTIVE);
    }

    customize->EndVisualGroup();

    // Advise the sink for the live check->edit coupling. Advise lives on
    // IFileDialog; the dialog QIs the sink for IFileDialogControlEvents
    // to deliver control notifications.
    sink_out.Attach(new DialogControlEvents(
        customize.Get(), cc.enable_check ? kCheckId : 0,
        cc.enable_edit ? kEditId : 0));
    hr = dialog->Advise(static_cast<IFileDialogEvents*>(sink_out.Get()),
                        &cookie_out);
    if (FAILED(hr)) {
        sink_out.Reset();
        cookie_out = 0;
    }
    return S_OK;
}

// Reads the final custom-control values after a confirmed Show().
void HarvestCustomControls(IFileDialog* dialog, const Options& options,
                           CustomResult& out) {
    const CustomControls& cc = options.custom;
    ComPtr<IFileDialogCustomize> customize;
    if (FAILED(dialog->QueryInterface(IID_PPV_ARGS(&customize))) ||
        !customize)
        return;

    if (cc.enable_check) {
        BOOL checked = FALSE;
        if (SUCCEEDED(customize->GetCheckButtonState(kCheckId, &checked)))
            out.checked = (checked == TRUE);
    }
    if (cc.enable_combo && !cc.combo_items.empty()) {
        DWORD sel = 0;
        if (SUCCEEDED(customize->GetSelectedControlItem(kComboId, &sel)) &&
            sel < cc.combo_items.size()) {
            out.combo_index = static_cast<int>(sel);
            out.combo_text = cc.combo_items[sel];
        }
    }
    if (cc.enable_edit) {
        LPWSTR text = nullptr;
        if (SUCCEEDED(customize->GetEditBoxText(kEditId, &text)) && text) {
            out.edit_text = ToUtf8(text);
            CoTaskMemFree(text);
        }
    }
}

std::string ShellItemPath(IShellItem* item) {
    if (!item) return {};
    LPWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path)
        return {};
    std::string out = ToUtf8(path);
    CoTaskMemFree(path);
    return out;
}

// Maps the Show() HRESULT onto the Result. Returns true when a hard
// error was recorded (cancel is a soft outcome: confirmed stays false).
bool MapShowResult(HRESULT hr, Result& result) {
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
    if (FAILED(hr)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "0x%08lX",
                      static_cast<unsigned long>(hr));
        result.error = std::string("IFileDialog::Show failed (hr=") + buf +
                       ")";
        return true;
    }
    result.confirmed = true;
    return false;
}

// Shared Show+teardown for the open/save runners.
void ShowAndTeardown(IFileDialog* dialog, const Options& options,
                     DialogControlEvents* sink, DWORD cookie,
                     HRESULT& show_hr) {
    show_hr = dialog->Show(static_cast<HWND>(options.owner));
    if (sink) dialog->Unadvise(cookie);
}

Result RunOpenFileDialog(const Options& options, bool pick_folders,
                         bool create_prompt) {
    Result result;
    ScopedComInit com;

    ComPtr<IFileOpenDialog> dialog;
    const HRESULT create_hr =
        CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                         IID_PPV_ARGS(&dialog));
    if (FAILED(create_hr) || !dialog) {
        result.error = "CoCreateInstance(CLSID_FileOpenDialog) failed";
        return result;
    }

    const auto filters = EffectiveFilters(options);
    std::vector<std::wstring> name_w, spec_w;
    std::vector<COMDLG_FILTERSPEC> table;
    BuildFilterTable(filters, name_w, spec_w, table);

    DWORD extra = FOS_FORCEFILESYSTEM;
    if (pick_folders) {
        extra |= FOS_PICKFOLDERS;
        if (create_prompt) extra |= FOS_CREATEPROMPT;
    } else {
        if (options.multi_select) extra |= FOS_ALLOWMULTISELECT;
        if (options.file_must_exist) extra |= FOS_FILEMUSTEXIST;
    }
    if (FAILED(ConfigureDialog(dialog.Get(), options, extra, table))) {
        result.error = "IFileOpenDialog configuration failed";
        return result;
    }

    ComPtr<DialogControlEvents> sink;
    DWORD cookie = 0;
    EmbedCustomControls(dialog.Get(), options, sink, cookie);

    HRESULT show_hr = E_FAIL;
    ShowAndTeardown(dialog.Get(), options, sink.Get(), cookie, show_hr);

    if (MapShowResult(show_hr, result)) return result;
    if (!result.confirmed) return result;

    if (options.multi_select && !pick_folders) {
        ComPtr<IShellItemArray> items;
        if (SUCCEEDED(dialog->GetResults(&items)) && items) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                ComPtr<IShellItem> item;
                if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
                    std::string p = ShellItemPath(item.Get());
                    if (!p.empty()) result.paths.push_back(std::move(p));
                }
            }
        }
    } else {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            std::string p = ShellItemPath(item.Get());
            if (!p.empty()) result.paths.push_back(std::move(p));
        }
    }

    HarvestCustomControls(dialog.Get(), options, result.custom);
    return result;
}

Result RunSaveFileDialog(const Options& options) {
    Result result;
    ScopedComInit com;

    ComPtr<IFileSaveDialog> dialog;
    const HRESULT create_hr =
        CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                         IID_PPV_ARGS(&dialog));
    if (FAILED(create_hr) || !dialog) {
        result.error = "CoCreateInstance(CLSID_FileSaveDialog) failed";
        return result;
    }

    const auto filters = EffectiveFilters(options);
    std::vector<std::wstring> name_w, spec_w;
    std::vector<COMDLG_FILTERSPEC> table;
    BuildFilterTable(filters, name_w, spec_w, table);

    DWORD extra = FOS_FORCEFILESYSTEM;
    if (options.overwrite_prompt) extra |= FOS_OVERWRITEPROMPT;
    if (FAILED(ConfigureDialog(dialog.Get(), options, extra, table))) {
        result.error = "IFileSaveDialog configuration failed";
        return result;
    }

    ComPtr<DialogControlEvents> sink;
    DWORD cookie = 0;
    EmbedCustomControls(dialog.Get(), options, sink, cookie);

    HRESULT show_hr = E_FAIL;
    ShowAndTeardown(dialog.Get(), options, sink.Get(), cookie, show_hr);

    if (MapShowResult(show_hr, result)) return result;
    if (!result.confirmed) return result;

    ComPtr<IShellItem> item;
    if (SUCCEEDED(dialog->GetResult(&item)) && item) {
        std::string p = ShellItemPath(item.Get());
        if (!p.empty()) result.paths.push_back(std::move(p));
    }
    HarvestCustomControls(dialog.Get(), options, result.custom);
    return result;
}

// ---- legacy comdlg32 fallback -------------------------------------------
// Reached only when the COM dialogs cannot be created. Cannot host the
// custom widgets — the caller marks used_legacy_fallback so the empty
// CustomResult is not misread as user input. Multi-select is supported
// for the open case via the standard dir\0f1\0f2\0\0 buffer layout.

Result LegacyOpenFile(const Options& options) {
    Result result;
    result.used_legacy_fallback = true;

    const auto filters = EffectiveFilters(options);
    const std::wstring filter_w = BuildLegacyFilterString(filters);
    const std::wstring initial_dir_w = ToWide(options.default_directory);
    const std::wstring def_ext_w = ToWide(options.default_extension);
    const std::wstring title_w = ToWide(options.title);

    std::wstring file_buf(32768, L'\0');
    if (!options.default_name.empty()) {
        const std::wstring def = ToWide(options.default_name);
        def.copy(file_buf.data(), file_buf.size() - 1);
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(options.owner);
    ofn.lpstrFilter = filter_w.c_str();
    ofn.lpstrFile = file_buf.data();
    ofn.nMaxFile = static_cast<DWORD>(file_buf.size());
    if (!initial_dir_w.empty()) ofn.lpstrInitialDir = initial_dir_w.c_str();
    if (!def_ext_w.empty()) ofn.lpstrDefExt = def_ext_w.c_str();
    if (!title_w.empty()) ofn.lpstrTitle = title_w.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (options.file_must_exist) ofn.Flags |= OFN_FILEMUSTEXIST;
    if (options.path_must_exist) ofn.Flags |= OFN_PATHMUSTEXIST;
    if (options.multi_select) ofn.Flags |= OFN_ALLOWMULTISELECT;

    if (!GetOpenFileNameW(&ofn)) {
        const DWORD err = CommDlgExtendedError();
        if (err != 0)
            result.error = "GetOpenFileNameW failed (ext err " +
                           std::to_string(err) + ")";
        return result;  // err==0 => user cancelled
    }
    result.confirmed = true;

    // Parse the return buffer: either "path\0" (single) or
    // "dir\0file1\0file2\0\0" (multi-select).
    const wchar_t* p = file_buf.data();
    std::wstring first(p);
    p += first.size() + 1;
    if (*p == L'\0') {
        result.paths.push_back(ToUtf8(first));
    } else {
        while (*p) {
            std::wstring name(p);
            result.paths.push_back(ToUtf8(first + L"\\" + name));
            p += name.size() + 1;
        }
    }
    return result;
}

Result LegacySaveFile(const Options& options) {
    Result result;
    result.used_legacy_fallback = true;

    const auto filters = EffectiveFilters(options);
    const std::wstring filter_w = BuildLegacyFilterString(filters);
    const std::wstring initial_dir_w = ToWide(options.default_directory);
    const std::wstring def_ext_w = ToWide(options.default_extension);
    const std::wstring title_w = ToWide(options.title);

    std::wstring file_buf(MAX_PATH, L'\0');
    if (!options.default_name.empty()) {
        const std::wstring def = ToWide(options.default_name);
        def.copy(file_buf.data(), file_buf.size() - 1);
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(options.owner);
    ofn.lpstrFilter = filter_w.c_str();
    ofn.lpstrFile = file_buf.data();
    ofn.nMaxFile = static_cast<DWORD>(file_buf.size());
    if (!initial_dir_w.empty()) ofn.lpstrInitialDir = initial_dir_w.c_str();
    if (!def_ext_w.empty()) ofn.lpstrDefExt = def_ext_w.c_str();
    if (!title_w.empty()) ofn.lpstrTitle = title_w.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (options.overwrite_prompt) ofn.Flags |= OFN_OVERWRITEPROMPT;
    if (options.path_must_exist) ofn.Flags |= OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) {
        const DWORD err = CommDlgExtendedError();
        if (err != 0)
            result.error = "GetSaveFileNameW failed (ext err " +
                           std::to_string(err) + ")";
        return result;
    }
    result.confirmed = true;
    result.paths.push_back(ToUtf8(file_buf.data()));
    return result;
}

Result LegacyPickDirectory(const Options& options) {
    Result result;
    result.used_legacy_fallback = true;
    // SHBrowseForFolder wants COM init on this thread for some shell
    // folders; COINIT_APARTMENTTHREADED is idempotent.
    ScopedComInit com;

    wchar_t display[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = static_cast<HWND>(options.owner);
    const std::wstring title_w =
        ToWide(options.title.empty() ? "Select folder" : options.title);
    bi.lpszTitle = title_w.c_str();
    // BIF_NEWDIALOGSTYLE gives the "New folder" button — the closest the
    // legacy picker gets to pick-or-create.
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.pszDisplayName = display;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return result;  // cancelled
    wchar_t path[MAX_PATH]{};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok) {
        result.error = "SHGetPathFromIDListW failed";
        return result;
    }
    result.confirmed = true;
    result.paths.push_back(ToUtf8(path));
    return result;
}

// Runs the modern dialog; falls back to the legacy picker only when the
// COM dialog could not even be created (no attempt ever reached Show).
template <typename Fallback>
Result WithFallback(Result&& modern, const Options& options,
                    Fallback&& fallback) {
    const bool creation_failed =
        !modern.confirmed && modern.paths.empty() &&
        modern.error.find("CoCreateInstance") != std::string::npos;
    if (!creation_failed) return std::move(modern);
    return fallback(options);
}

}  // namespace

Result OpenFileRead(const Options& options) {
    std::string verr;
    if (!ValidateOptions(options, verr)) {
        Result r;
        r.error = std::move(verr);
        return r;
    }
    return WithFallback(RunOpenFileDialog(options, false, false), options,
                        LegacyOpenFile);
}

Result OpenFileWrite(const Options& options) {
    std::string verr;
    if (!ValidateOptions(options, verr)) {
        Result r;
        r.error = std::move(verr);
        return r;
    }
    return WithFallback(RunSaveFileDialog(options), options, LegacySaveFile);
}

Result OpenDirectoryRead(const Options& options) {
    std::string verr;
    if (!ValidateOptions(options, verr)) {
        Result r;
        r.error = std::move(verr);
        return r;
    }
    return WithFallback(RunOpenFileDialog(options, true, false), options,
                        LegacyPickDirectory);
}

Result OpenDirectoryWrite(const Options& options) {
    std::string verr;
    if (!ValidateOptions(options, verr)) {
        Result r;
        r.error = std::move(verr);
        return r;
    }
    return WithFallback(RunOpenFileDialog(options, true, options.create_prompt),
                        options, LegacyPickDirectory);
}

}  // namespace XER::Content::OpenDialog

#else  // !_WIN32 — cross-platform stub (module is Windows-only for now)

namespace XER::Content::OpenDialog {
namespace {
Result Unsupported() {
    Result r;
    r.error = "opendialog is Windows-only on this build";
    return r;
}
}  // namespace

Result OpenFileRead(const Options&) { return Unsupported(); }
Result OpenFileWrite(const Options&) { return Unsupported(); }
Result OpenDirectoryRead(const Options&) { return Unsupported(); }
Result OpenDirectoryWrite(const Options&) { return Unsupported(); }

}  // namespace XER::Content::OpenDialog

#endif  // _WIN32

// ---- shared, platform-neutral pieces ------------------------------------

namespace XER::Content::OpenDialog {

std::future<Result> OpenFileReadAsync(Options options) {
    return std::async(std::launch::async,
                      [o = std::move(options)] { return OpenFileRead(o); });
}
std::future<Result> OpenFileWriteAsync(Options options) {
    return std::async(std::launch::async,
                      [o = std::move(options)] { return OpenFileWrite(o); });
}
std::future<Result> OpenDirectoryReadAsync(Options options) {
    return std::async(std::launch::async,
                      [o = std::move(options)] {
                          return OpenDirectoryRead(o);
                      });
}
std::future<Result> OpenDirectoryWriteAsync(Options options) {
    return std::async(std::launch::async,
                      [o = std::move(options)] {
                          return OpenDirectoryWrite(o);
                      });
}

std::vector<Filter> ParseFilterSpec(std::string_view spec) {
    std::vector<Filter> out;
    size_t pos = 0;
    const auto next_token = [&](std::string_view& tok) -> bool {
        if (pos > spec.size()) return false;
        const size_t bar = spec.find('|', pos);
        tok = (bar == std::string_view::npos)
                  ? spec.substr(pos)
                  : spec.substr(pos, bar - pos);
        pos = (bar == std::string_view::npos) ? spec.size() + 1 : bar + 1;
        return true;
    };
    std::string_view name;
    while (next_token(name)) {
        std::string_view patterns;
        if (!next_token(patterns)) break;  // dangling name — ignored
        if (!name.empty() && !patterns.empty())
            out.push_back({std::string(name), std::string(patterns)});
    }
    return out;
}

std::wstring BuildLegacyFilterString(const std::vector<Filter>& filters) {
    std::wstring out;
#ifdef _WIN32
    for (const Filter& f : filters) {
        out += ToWide(f.name);
        out += L'\0';
        out += ToWide(f.spec);
        out += L'\0';
    }
#else
    for (const Filter& f : filters) {
        for (char c : f.name) out += static_cast<wchar_t>(c);
        out += L'\0';
        for (char c : f.spec) out += static_cast<wchar_t>(c);
        out += L'\0';
    }
#endif
    out += L'\0';  // terminating extra NUL
    return out;
}

bool ValidateOptions(const Options& options, std::string& error) {
    const CustomControls& cc = options.custom;
    if (cc.enable_combo) {
        if (cc.combo_items.empty()) {
            error = "custom combo enabled but combo_items is empty";
            return false;
        }
        if (cc.combo_default_index < 0 ||
            cc.combo_default_index >=
                static_cast<int>(cc.combo_items.size())) {
            error = "combo_default_index out of range";
            return false;
        }
    }
    for (const Filter& f : options.filters) {
        if (f.spec.empty()) {
            error = "filter '" + f.name + "' has an empty spec";
            return false;
        }
    }
    return true;
}

int RunSelftest(std::string& report) {
    int failures = 0;
    const auto check = [&](bool cond, const char* msg) {
        report += cond ? "PASS  " : "FAIL  ";
        report += msg;
        report += '\n';
        if (!cond) ++failures;
    };

    // --- ParseFilterSpec ---
    {
        const auto f = ParseFilterSpec("Images|*.png;*.jpg|All files|*.*");
        check(f.size() == 2, "ParseFilterSpec: two pairs parsed");
        check(f[0].name == "Images" && f[0].spec == "*.png;*.jpg",
              "ParseFilterSpec: first pair fields");
        check(f[1].name == "All files" && f[1].spec == "*.*",
              "ParseFilterSpec: second pair fields");
        check(ParseFilterSpec("dangling-name|").empty(),
              "ParseFilterSpec: empty spec dropped");
        check(ParseFilterSpec("no-separator").empty(),
              "ParseFilterSpec: lone token ignored");
        check(ParseFilterSpec("").empty(), "ParseFilterSpec: empty input");
    }

    // --- BuildLegacyFilterString ---
    {
        const std::wstring s = BuildLegacyFilterString(
            {{"Images", "*.png"}, {"All files", "*.*"}});
        // Expected layout: "Images\0*.png\0All files\0*.*\0" + one extra
        // terminating NUL from the literal itself.
        const wchar_t expected[] = L"Images\0*.png\0All files\0*.*\0";
        const std::wstring expected_s(
            expected, sizeof(expected) / sizeof(wchar_t));
        check(s == expected_s,
              "BuildLegacyFilterString: double-NUL layout");
        check(BuildLegacyFilterString({}) == std::wstring(L"\0", 1),
              "BuildLegacyFilterString: empty -> terminator only");
    }

    // --- ValidateOptions ---
    {
        Options ok;
        ok.custom.enable_combo = true;
        ok.custom.combo_items = {"utf-8", "utf-16"};
        ok.custom.combo_default_index = 1;
        std::string err;
        check(ValidateOptions(ok, err), "ValidateOptions: sane options");

        Options bad = ok;
        bad.custom.combo_default_index = 5;
        check(!ValidateOptions(bad, err) && !err.empty(),
              "ValidateOptions: combo index out of range rejected");

        Options empty_combo;
        empty_combo.custom.enable_combo = true;
        check(!ValidateOptions(empty_combo, err),
              "ValidateOptions: enabled combo needs items");

        Options bad_filter;
        bad_filter.filters.push_back({"Empty", ""});
        check(!ValidateOptions(bad_filter, err),
              "ValidateOptions: empty filter spec rejected");
    }

    // --- Result plumbing (defaults / confirmed flag contract) ---
    {
        Result r;
        check(!r.confirmed && r.paths.empty() && r.error.empty() &&
                  !r.used_legacy_fallback && r.custom.combo_index == -1,
              "Result: zero-init contract");
        r.confirmed = true;
        r.paths.push_back("C:/tmp/a.txt");
        r.custom.checked = true;
        r.custom.combo_index = 0;
        r.custom.combo_text = "utf-8";
        check(r.confirmed && r.paths.size() == 1 && r.custom.checked,
              "Result: fields round-trip");
    }

#ifndef _WIN32
    // On non-Windows builds the pickers must report the stub error.
    {
        Options o;
        const Result r = OpenFileRead(o);
        check(!r.confirmed && !r.error.empty(),
              "non-Windows stub returns error, not crash");
    }
#else
    report +=
        "SKIP  interactive COM dialog checks (need a manual run — use\n"
        "      XER_OpenDialog_Smoke --dlg-open|--dlg-save|--dlg-dir|"
        "--dlg-dir-write)\n";
#endif

    return failures;
}

}  // namespace XER::Content::OpenDialog
