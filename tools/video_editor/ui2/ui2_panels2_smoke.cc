// ui2_panels2 smoke — see ui2_panels2_smoke.h. All checks drive the
// public panel verbs + shell hooks directly; rendering is never
// invoked (the paint contract is structural, not pixel).
#include "ui2_panels2_smoke.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "extensions_panel.h"
#include "history_panel.h"
#include "markers_panel.h"
#include "queue_panel.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "transcript_edit.h"
#include "transcript_panel.h"
#include "ve_app.h"
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui2 {
namespace {

bool NearlyEqual(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

// ---- fixtures -------------------------------------------------------

std::vector<Ai::TranscriptSegment> FakeSegments() {
    // Two whisper-style segments spanning source [0, 6) with REAL
    // word spans — the verbatim-timing path (the proportional
    // fallback distributes by text length, which would fuzz the
    // expected seeks here).
    return {
        {0.0, 4.0, "hello brave new world",
         {
             {0.0, 1.0, "hello"},
             {1.0, 2.0, "brave"},
             {2.0, 3.0, "new"},
             {3.0, 4.0, "world"},
         }},
        {5.0, 6.0, "a bb",
         {
             {5.0, 5.5, "a"},
             {5.5, 6.0, "bb"},
         }},
    };
}

Clip BoundClip() {
    // tl 10, in_point 2, duration 4 @ rate 1 -> consumes src [2,6).
    Clip clip;
    clip.id = "tr-clip";
    clip.source_path = "D:/media/interview.mp4";
    clip.source.kind = SourceKind::Media;
    clip.track = 0;
    clip.timeline_start = 10.0;
    clip.in_point = 2.0;
    clip.duration = 4.0;
    clip.playback_rate = 1.0;
    return clip;
}

Clip TailClip() {
    Clip clip;
    clip.id = "tr-tail";
    clip.source_path = "D:/media/broll.mp4";
    clip.source.kind = SourceKind::Media;
    clip.track = 0;
    clip.timeline_start = 16.0;
    clip.in_point = 0.0;
    clip.duration = 2.0;
    clip.playback_rate = 1.0;
    return clip;
}

// Seek clamps to project.duration() — the marker/history checks
// need a non-empty sequence so the playhead can actually move.
void GiveSequence(VeApp* app) {
    Project project;
    Track v1;
    v1.kind = TrackKind::Video;
    v1.name = "V1";
    Clip filler;
    filler.id = "filler";
    filler.source_path = "D:/media/filler.mp4";
    filler.source.kind = SourceKind::Media;
    filler.track = 0;
    filler.timeline_start = 0.0;
    filler.in_point = 0.0;
    filler.duration = 60.0;
    filler.playback_rate = 1.0;
    project.tracks = {v1};
    project.clips = {filler};
    app->SetProject(project);
}

std::shared_ptr<VeApp> MakeApp() {
    const auto prefs_path =
        std::filesystem::temp_directory_path() /
        "xer_ui2_panels2_smoke" / "ui2_shell.json";
    return std::make_shared<VeApp>(std::string{}, prefs_path);
}

// ---- 1. markers: shell hook -> history -> project -> panel -----------

std::string CheckMarkersSync() {
    auto app = MakeApp();
    if (app->Markers() == nullptr) return "markers panel missing";
    GiveSequence(app.get());
    app->Seek(5.0);
    app->Markers()->AddAtPlayhead();
    const auto& markers = app->Document()->markers;
    if (markers.size() != 1 || !NearlyEqual(markers[0].time, 5.0)) {
        return "add-at-playhead did not land on the project";
    }
    // Sync: NotifyTimelineChanged pushed the model into the panel.
    const auto times = app->Markers()->VisibleTimes();
    if (times.size() != 1 || !NearlyEqual(times[0], 5.0)) {
        return "panel did not sync the model marker";
    }
    // Search filtering on the panel side.
    app->Markers()->SetSearchText("zzz");
    if (app->Markers()->VisibleCount() != 0) {
        return "search filter did not hide the row";
    }
    app->Markers()->SetSearchText("");
    // Seek path: double-click/activate -> on_seek -> bridge -> playhead.
    app->Seek(0.0);
    app->Markers()->SeekToMarker(0);
    if (!NearlyEqual(app->PlayheadSeconds(), 5.0)) {
        return "marker seek did not move the playhead";
    }
    // Rename through the update hook (remove+add macro).
    app->Markers()->RenameMarkerAt(0, "Intro");
    if (app->Document()->markers.size() != 1 ||
        app->Document()->markers[0].name != "Intro") {
        return "marker rename did not land";
    }
    // Session annotation store (panel-level data — survives re-feeds).
    app->Markers()->SetMarkerAnnotation(0, 2.5, "needs review");
    double dur = 0.0;
    std::string comment;
    if (!app->Markers()->AnnotationFor(5.0, &dur, &comment) ||
        !NearlyEqual(dur, 2.5) || comment != "needs review") {
        return "annotation store failed";
    }
    app->Markers()->DeleteMarkerAt(0);
    if (!app->Document()->markers.empty()) {
        return "marker delete did not land";
    }
    if (app->Markers()->VisibleCount() != 0) {
        return "panel did not drop the deleted row";
    }
    return {};
}

// ---- 2. history: enumeration + row ordering + jump --------------------

std::string CheckHistoryEnumeration() {
    // Bare model check — label ordering is the panel's input contract.
    Project project;
    TimelineHistory history(project);
    if (history.AddTrack(TrackKind::Video, "V1") != 0) {
        return "AddTrack failed";
    }
    Marker m;
    m.time = 1.0;
    m.name = "one";
    std::string err;
    if (!history.AddMarker(m, &err)) return "AddMarker failed";
    const auto undo = history.UndoLabels();
    if (undo.size() != 2) {
        return "UndoLabels size " + std::to_string(undo.size());
    }

    auto panel = HistoryPanel::Create();
    panel->SetEntries(undo, {});
    // Rows: [0..1] undo newest-first, row 2 divider.
    if (panel->RowCount() != 3 || panel->UndoCount() != 2) {
        return "row space wrong";
    }
    if (panel->KindAt(0) != HistoryPanel::RowKind::Undo ||
        panel->KindAt(2) != HistoryPanel::RowKind::Divider) {
        return "row kinds wrong";
    }
    if (panel->LabelAt(0) != undo[1] || panel->StepsAt(0) != 1 ||
        panel->StepsAt(1) != 2) {
        return "undo ordering/steps wrong";
    }
    int undo_steps = -1, redo_steps = -1;
    HistoryPanel::Hooks hooks;
    hooks.on_undo_steps = [&undo_steps](int n) { undo_steps = n; };
    hooks.on_redo_steps = [&redo_steps](int n) { redo_steps = n; };
    panel->SetHooks(hooks);
    if (panel->JumpToRow(2)) return "divider jump should be a no-op";
    if (!panel->JumpToRow(1) || undo_steps != 2) {
        return "undo-row jump did not emit 2 steps";
    }
    // Redo side: divider then next-to-redo first.
    panel->SetEntries({}, {"a", "b"});
    if (panel->KindAt(1) != HistoryPanel::RowKind::Redo ||
        panel->LabelAt(1) != "a" || panel->StepsAt(2) != 2) {
        return "redo ordering/steps wrong";
    }
    if (!panel->JumpToRow(2) || redo_steps != 2) {
        return "redo-row jump did not emit 2 steps";
    }
    return {};
}

std::string CheckHistoryJump() {
    auto app = MakeApp();
    GiveSequence(app.get());
    // Two marker edits -> two undo steps; the panel click moves
    // through them.
    app->Seek(1.0);
    app->Markers()->AddAtPlayhead();
    app->Seek(2.0);
    app->Markers()->AddAtPlayhead();
    if (app->History() == nullptr) return "history panel missing";
    if (app->History()->UndoCount() < 2) {
        return "expected >=2 undo entries, got " +
               std::to_string(app->History()->UndoCount());
    }
    // Row 0 = newest undo -> 1 step.
    if (!app->History()->JumpToRow(0)) return "jump to newest failed";
    if (app->Document()->markers.size() != 1 ||
        !NearlyEqual(app->Document()->markers[0].time, 1.0)) {
        return "one undo did not restore the first marker";
    }
    // Now undo stack 0 entries? No — UndoCount() is at least 1 (the
    // first AddMarker). The redo row sits at UndoCount()+1.
    const int redo_row = app->History()->UndoCount() + 1;
    if (app->History()->KindAt(redo_row) !=
        HistoryPanel::RowKind::Redo) {
        return "expected a redo row after the undo";
    }
    if (!app->History()->JumpToRow(redo_row)) {
        return "redo jump failed";
    }
    if (app->Document()->markers.size() != 2) {
        return "redo did not restore the second marker";
    }
    return {};
}

// ---- 3. queue: deterministic fake-job lifecycle -----------------------

std::string CheckQueueFakeJob() {
    auto panel = QueuePanel::Create();
    // No hooks -> simulated drive (the smoke's fake-job seam).
    if (!panel->IsSimulated()) {
        return "panel should be simulated without on_submit";
    }
    QueueJob job;
    job.id = "fake-1";
    job.name = "interview.mp4";
    job.format = "H.264 MP4";
    job.output_path = "D:/out/interview.mp4";
    job.sim_total_seconds = 2.0;
    if (panel->Enqueue(std::move(job)) < 0) {
        return "enqueue failed";
    }
    // The single-worker simulation promotes the head immediately.
    const QueueJob* j = panel->JobById("fake-1");
    if (j == nullptr || j->state != QueueJobState::Running) {
        return "head job did not start";
    }
    panel->Tick(1.0);
    j = panel->JobById("fake-1");
    if (j == nullptr || !NearlyEqual(j->progress, 0.5)) {
        return "sim progress wrong: " +
               std::to_string(j != nullptr ? j->progress : -1.0);
    }
    // Simulated pause freezes the row (the real engine reports
    // unsupported through on_pause — see queue_panel.h).
    if (!panel->PauseJob("fake-1") ||
        panel->JobById("fake-1")->state != QueueJobState::Paused) {
        return "sim pause failed";
    }
    panel->Tick(1.0);
    if (!NearlyEqual(panel->JobById("fake-1")->progress, 0.5)) {
        return "paused job still advanced";
    }
    if (!panel->ResumeJob("fake-1") ||
        panel->JobById("fake-1")->state != QueueJobState::Running) {
        return "sim resume failed";
    }
    panel->Tick(1.0);
    j = panel->JobById("fake-1");
    if (j == nullptr || j->state != QueueJobState::Done ||
        !NearlyEqual(j->progress, 1.0)) {
        return "job did not finish";
    }
    panel->ClearFinished();
    if (panel->JobCount() != 0) return "ClearFinished left rows";

    // Cancel + retry path on a second job.
    QueueJob j2;
    j2.id = "fake-2";
    j2.name = "b.mp4";
    j2.sim_total_seconds = 10.0;
    panel->Enqueue(std::move(j2));
    panel->Tick(0.5);
    if (!panel->CancelJob("fake-2") ||
        panel->JobById("fake-2")->state != QueueJobState::Cancelled) {
        return "cancel failed";
    }
    // A cancelled row can't retry — only Failed can.
    if (panel->RetryJob("fake-2")) {
        return "retry on cancelled should fail";
    }
    panel->SetJobState("fake-2", QueueJobState::Failed, "disk full");
    // Retry requeues and — as the only row — the simulated driver
    // promotes the head straight to Running.
    if (!panel->RetryJob("fake-2") ||
        panel->JobById("fake-2")->state != QueueJobState::Running) {
        return "failed-job retry did not requeue";
    }
    return {};
}

// ---- 4. extensions: enumerate + session gating ------------------------

std::string CheckExtensions() {
    auto panel = ExtensionsPanel::Create();
    std::vector<ExtensionRow> fixture(2);
    fixture[0].id = "beatcutter";
    fixture[0].name = "Beat Cutter";
    fixture[0].version = "1.2";
    fixture[0].path = "D:/assets/extensions/beatcutter.xexrj";
    fixture[0].verified = true;
    fixture[0].has_ui = true;
    fixture[0].entry = "main.js";
    fixture[0].ui_member = "index.html";
    fixture[0].status = "verified";
    fixture[1].id = "broken";
    fixture[1].name = "Broken Ext";
    fixture[1].verified = false;
    fixture[1].status = "crc mismatch";
    fixture[1].errors = {"crc mismatch"};

    ExtensionsPanel::Hooks hooks;
    hooks.enumerate = [&fixture] { return fixture; };
    std::string ran_id, ui_id, disabled_id;
    bool disabled_value = true;
    hooks.on_run = [&ran_id](const std::string& id) { ran_id = id; };
    hooks.on_open_ui = [&ui_id](const std::string& id) { ui_id = id; };
    hooks.on_set_enabled =
        [&disabled_id, &disabled_value](const std::string& id, bool on) {
            disabled_id = id;
            disabled_value = on;
        };
    panel->SetHooks(hooks);
    panel->Reload();
    if (panel->ExtensionCount() != 2 ||
        panel->ExtensionById("beatcutter") == nullptr) {
        return "enumerate did not feed rows";
    }
    // Unverified rows can't run or open UI.
    if (panel->RunExtension("broken") || !ran_id.empty()) {
        return "unverified extension ran";
    }
    if (!panel->RunExtension("beatcutter") || ran_id != "beatcutter") {
        return "verified extension did not run";
    }
    if (!panel->OpenExtensionUi("beatcutter") || ui_id != "beatcutter") {
        return "UI open did not fire";
    }
    // Session enable/disable — panel-level state + shell hook.
    if (!panel->SetEnabled("beatcutter", false) ||
        disabled_id != "beatcutter" || disabled_value) {
        return "disable did not fire the hook";
    }
    if (panel->ExtensionById("beatcutter")->enabled) {
        return "row still enabled";
    }
    if (panel->RunExtension("beatcutter")) {
        return "disabled extension ran";
    }
    panel->SetEnabled("beatcutter", true);
    // Settings seam — records the id (the dialog needs a tree to paint).
    if (!panel->OpenSettings("beatcutter") ||
        panel->LastSettingsId() != "beatcutter") {
        return "settings seam failed";
    }
    return {};
}

// ---- 5/6. transcript: segment seek + selection verbs -------------------

std::string CheckTranscriptSeek() {
    auto app = MakeApp();
    Project project;
    Track v1;
    v1.kind = TrackKind::Video;
    v1.name = "V1";
    project.tracks = {v1};
    project.clips = {BoundClip(), TailClip()};
    app->SetProject(project);

    app->BindTranscriptToClip("tr-clip");
    if (app->Transcript() == nullptr) return "transcript panel missing";
    app->InjectTranscript(FakeSegments());
    if (app->Transcript()->WordCount() != 6 ||
        app->Transcript()->SegmentCount() != 2) {
        return "fake transcript did not expand to 6 words";
    }
    // Word 2 ("new") starts at src 2.0 -> tl 10.0 through the bound
    // clip's in_point/rate map.
    if (!app->Transcript()->ActivateWord(2) ||
        !NearlyEqual(app->PlayheadSeconds(), 10.0)) {
        return "word seek did not map through the clip";
    }
    // Word 0 (src 0) sits outside the consumed span -> no seek.
    const double before = app->PlayheadSeconds();
    if (app->Transcript()->ActivateWord(0) ||
        !NearlyEqual(app->PlayheadSeconds(), before)) {
        return "out-of-clip word should not seek";
    }
    // Segment navigation: segment 1 starts at src 5 -> tl 13.
    if (!app->Transcript()->JumpToSegment(1) ||
        !NearlyEqual(app->PlayheadSeconds(), 13.0)) {
        return "segment jump mis-mapped";
    }
    // Playhead -> live word: tl 13 -> src 5 -> word index 4. The
    // panel's live highlight reads the shell playhead on Tick.
    app->Tick(0.016);
    if (app->Transcript()->CurrentWord() != 4) {
        return "live word mismatch: " +
               std::to_string(app->Transcript()->CurrentWord());
    }
    return {};
}

std::string CheckTranscriptEdit() {
    auto app = MakeApp();
    Project project;
    Track v1;
    v1.kind = TrackKind::Video;
    v1.name = "V1";
    project.tracks = {v1};
    project.clips = {BoundClip(), TailClip()};
    app->SetProject(project);
    app->BindTranscriptToClip("tr-clip");
    app->InjectTranscript(FakeSegments());

    // Select words 2..3 ("new world") -> source [2,4) -> tl [10,12)
    // through the bound clip's map (tl 10 + (src - in_point 2)).
    app->Transcript()->SelectWordRange(2, 3);
    double t0 = -1, t1 = -1;
    if (!app->Transcript()->SelectionTimelineRange(&t0, &t1) ||
        !NearlyEqual(t0, 10.0) || !NearlyEqual(t1, 12.0)) {
        return "selection did not map to tl [10,12)";
    }
    if (app->Transcript()->SelectedText() != "new world") {
        return "selected text wrong: " +
               app->Transcript()->SelectedText();
    }
    // Marker at the selection head.
    if (!app->Transcript()->AddMarkerForSelection()) {
        return "marker action failed";
    }
    if (app->Document()->markers.size() != 1 ||
        !NearlyEqual(app->Document()->markers[0].time, 10.0) ||
        app->Document()->markers[0].name != "new world") {
        return "selection marker did not land";
    }
    // Captions: BuildCaptionClips over the same source range.
    const std::size_t clips_before = app->Document()->clips.size();
    if (!app->Transcript()->MakeCaptionsForSelection()) {
        return "captions action failed";
    }
    if (app->Document()->clips.size() <= clips_before) {
        return "no caption clips were added";
    }
    // Undo the caption add so the ripple-delete range is clean.
    app->Undo();
    if (app->Document()->clips.size() != clips_before) {
        return "caption undo did not restore the clip set";
    }
    // Ripple delete: extract tl [10,12) -> the follower clip shifts
    // 16 -> 14 (the bound clip keeps a [12,14) remainder piece).
    if (!app->Transcript()->RippleDeleteSelection()) {
        return "ripple delete hook did not fire";
    }
    const Clip* tail = FindClip(*app->Document(), "tr-tail");
    if (tail == nullptr || !NearlyEqual(tail->timeline_start, 14.0)) {
        return "ripple delete did not shift the follower clip";
    }
    // Undo restores the project.
    app->Undo();
    tail = FindClip(*app->Document(), "tr-tail");
    if (tail == nullptr || !NearlyEqual(tail->timeline_start, 16.0)) {
        return "ripple undo did not restore the project";
    }
    return {};
}

}  // namespace

std::string Ui2Panels2Smoke() {
    if (const std::string err = CheckMarkersSync(); !err.empty()) {
        return "markers: " + err;
    }
    if (const std::string err = CheckHistoryEnumeration();
        !err.empty()) {
        return "history enum: " + err;
    }
    if (const std::string err = CheckHistoryJump(); !err.empty()) {
        return "history jump: " + err;
    }
    if (const std::string err = CheckQueueFakeJob(); !err.empty()) {
        return "queue: " + err;
    }
    if (const std::string err = CheckExtensions(); !err.empty()) {
        return "extensions: " + err;
    }
    if (const std::string err = CheckTranscriptSeek(); !err.empty()) {
        return "transcript seek: " + err;
    }
    if (const std::string err = CheckTranscriptEdit(); !err.empty()) {
        return "transcript edit: " + err;
    }
    return {};
}

}  // namespace XER::VideoEditor::Ui2
