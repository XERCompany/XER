#include "ui2_lifecycle_smoke.h"

#include "helper/path_utils.h"

#include "export_dialog.h"
#include "project_settings_dialog.h"
#include "project_store.h"
#include "queue_panel.h"
#include "timeline_model.h"
#include "ve_app.h"

#include <filesystem>
#include <string>

namespace XER::VideoEditor::Ui2 {

namespace {

// A temp workspace the check owns: prefs file + project file +
// autosave dir all live under here so nothing touches the user's
// real ui2_shell.json or documents.
struct SmokeDir {
    std::filesystem::path root;
    explicit SmokeDir(std::string name)
        : root(std::filesystem::temp_directory_path() /
               "xer_ui2_lifecycle" / std::move(name)) {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
    }
    ~SmokeDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    std::string U8(const std::filesystem::path& p) const {
        // char8_t-safe: generic_u8string() yields std::u8string under
        // C++20 — Path::ToUtf8 returns the std::string this needs.
        return XER::Helper::Path::ToUtf8(p);
    }
};

}  // namespace

std::string Ui2LifecycleSmoke() {
    SmokeDir dir{"ui2_lifecycle"};
    if (!std::filesystem::exists(dir.root)) {
        return "temp dir unavailable";
    }
    const auto prefs_path = dir.root / "ui2_shell_test.json";
    const auto project_path = dir.root / "project.xerproj";

    auto app = std::make_shared<VeApp>("", prefs_path);
    if (app == nullptr) return "VeApp ctor returned null";

    // Seed a non-empty document — export validation needs
    // duration() > 0 (an empty timeline has nothing to export).
    Project doc;
    doc.width = 640;
    doc.height = 360;
    Track v;
    v.kind = TrackKind::Video;
    v.name = "V1";
    doc.tracks.push_back(std::move(v));
    Clip clip;
    clip.id = "c1";
    clip.track = 0;
    clip.timeline_start = 0.0;
    clip.duration = 4.0;
    doc.clips.push_back(std::move(clip));
    app->SetProject(doc);

    // ── export dialog: seed + validation + submit → queue row ──
    app->ShowExportDialog();
    ExportDialog* form = app->ExportForm();
    if (form == nullptr) return "export form null after show";
    if (form->GetRequest().output_path.empty()) {
        return "export dialog did not seed an output path";
    }
    form->SetOutputPath("");
    if (form->Validate().empty()) {
        return "export validation accepted empty output path";
    }
    form->SetOutputPath(dir.U8(dir.root / "out.mp4"));
    if (!form->Validate().empty()) {
        return "export validation rejected valid request: " +
               form->ErrorText();
    }
    const int jobs_before =
        app->ExportQueue() != nullptr ? app->ExportQueue()->JobCount()
                                      : -1;
    app->SubmitExportForm();
    if (app->ExportQueue() == nullptr ||
        app->ExportQueue()->JobCount() != jobs_before + 1) {
        return "export submit did not enqueue a queue row";
    }

    // ── project settings apply → undo ────────────────────────────
    Ui2ProjectSettings settings;
    settings.width = 800;
    settings.height = 450;
    settings.fps_num = 24;
    settings.fps_den = 1;
    settings.audio_sample_rate = 44100;
    settings.scratch_dir = dir.U8(dir.root / "scratch");
    settings.autosave_interval_s = 5;
    const int before_w = app->Document()->width;
    const int before_h = app->Document()->height;
    app->ApplyProjectSettings(settings);
    if (app->Document()->width != 800 ||
        app->Document()->height != 450) {
        return "settings apply did not change the canvas";
    }
    if (app->Prefs().audio_sample_rate != 44100) {
        return "settings apply did not commit app prefs";
    }
    app->Undo();
    if (app->Document()->width != before_w ||
        app->Document()->height != before_h) {
        return "settings undo did not restore the canvas";
    }

    // ── dirty flag: edit → dirty, save → clean ───────────────────
    app->SetProject(doc);   // document replace counts as an edit
    if (!app->IsProjectDirty()) {
        return "edit did not arm the dirty flag";
    }
    if (!app->SaveProjectAs(dir.U8(project_path))) {
        return "SaveProjectAs failed";
    }
    if (app->IsProjectDirty()) {
        return "save did not clear the dirty flag";
    }

    // ── autosave: Tick pump past the poll cadence writes a slot ──
    app->SetAutosaveIntervalMs(0);   // due immediately when dirty
    app->SetProject(doc);            // re-arm dirty (path stays bound)
    if (!app->IsProjectDirty()) {
        return "second edit did not re-arm dirty";
    }
    app->Tick(2.5);                  // > 2 s poll cadence
    const auto slots = app->AutosaveSlots();
    if (!app->AutosaveInitialized() || slots.empty()) {
        return "autosave tick did not write a slot";
    }
    if (!std::filesystem::exists(
            XER::Helper::Path::FromUtf8(slots.front()))) {
        return "autosave slot missing on disk";
    }

    // ── close guard: dirty close blocks until resolved ───────────
    bool approved = false;
    app->SetOnCloseApproved([&approved] { approved = true; });
    if (app->RequestClose()) {
        return "dirty close passed the guard without a prompt";
    }
    if (!app->CloseGuardPromptOpen()) {
        return "dirty close did not open the prompt";
    }
    app->ResolveCloseGuard(VeApp::CloseGuardChoice::Cancel);
    if (app->CloseGuardPromptOpen() || approved) {
        return "cancel did not keep the close blocked";
    }
    if (app->RequestClose()) {
        return "re-close passed while still dirty";
    }
    app->ResolveCloseGuard(VeApp::CloseGuardChoice::Discard);
    if (!approved) {
        return "discard did not approve the close";
    }
    // The host re-posts WM_CLOSE after approval — the same guard must
    // let it through once (latch consumes).
    if (!app->RequestClose()) {
        return "approved close was re-blocked by the guard";
    }

    // Save-resolve path: dirty again, prompt, Save writes + approves.
    approved = false;
    app->SetProject(doc);
    if (app->RequestClose()) {
        return "second dirty close passed the guard";
    }
    app->ResolveCloseGuard(VeApp::CloseGuardChoice::Save);
    if (!approved || app->IsProjectDirty()) {
        return "save-resolve did not approve + clear dirty";
    }
    if (!app->RequestClose()) {
        return "approved save-close was re-blocked";
    }
    // A clean project closes straight through.
    if (!app->RequestClose()) {
        return "clean close was blocked by the guard";
    }

    return "";
}

}  // namespace XER::VideoEditor::Ui2
