// Implementation of smoke_checks.h — see the header for the contract.
// Each check returns an empty string on success or a one-line failure
// reason. Keep every check synchronous and cheap: the whole suite is
// expected to finish in a few seconds; the watchdog in RunAll() is
// the hard cap, not the budget.
#include "smoke_checks.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <json/json.h>

#include "ai_tags.h"
#include "app_runtime/app_runtime.h"
#include "app_runtime/js_engine.h"  // complete type for BootResult dtor
#include "audio_mixer.h"
#include "audio_sync.h"
#include "cef/cef_ext_scheme.h"
#include "compositor.h"
#include "content/ai/find_object/find_object.h"  // YA.AIFindObject
#include "content/video/video_info.h"
#include "export_engine.h"
#include "export_preset.h"
#include "extensions/ext_host.h"
#include "font_catalog.h"
#include "frame.h"
#include "frame_exporter.h"
#include "frame_render/frame_render.h"   // YA.FrameRender
#include "generated_source.h"
#include "gpu_video/cuda_decode.h"       // YA.CUDADecode
#include "gpu_video/cuda_encode.h"       // YA.CUDAEncode
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "js_fx.h"
#include "keyboard_map.h"
#include "lut_manager.h"
#include "media_catalog.h"
#include "media_decoder.h"               // MakeGpuDecoder / IMediaDecoder
#include "media_worker_client.h"
#include "media_worker_protocol.h"
#include "nn_probe.h"
#include "poster_cache.h"
#include "project_autosave.h"
#include "project_store.h"
#include "records_store.h"
#include "rendering/renderer.h"          // Renderer seam for FrameRender check
#include "rendering/render_store.h"      // store-transport smoke path
#include "sequence_preset.h"
#include "source_edit.h"
#include "timecode.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "tz/tz.h"
#include "url_import.h"
#include "worker_pipe.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

#if defined(_WIN32)
#include <windows.h>   // GetModuleFileNameW — worker-exe sibling lookup
#endif

namespace XER::VideoEditor {
namespace {

// Canonical importable-media extension list — the single source of
// truth behind MediaImportExtensions(); see smoke_checks.h for why it
// lives in the UI-free core.
const std::string kMediaImportExtensions =
    // video containers FFmpeg demuxes
    "mp4;m4v;mkv;mov;qt;avi;divx;wmv;asf;webm;mpg;mpeg;mpe;m1v;m2v;mpv;"
    "ts;mts;m2ts;vob;3gp;3g2;flv;f4v;mxf;dv;dif;ogv;rm;rmvb;y4m;nut;bik;amv;"
    // audio-only containers
    "wav;mp3;m4a;m4b;aac;flac;ogg;oga;opus;wma;aif;aiff;aifc;mka;ac3;"
    "eac3;dts;amr;au;caf;wv;ape;tta;spx;voc;gsm;oma;"
    // still images (FFmpeg image2 + OIIO set)
    "png;jpg;jpeg;jpe;bmp;dib;gif;tif;tiff;webp;tga;ppm;pgm;pbm;pam;xpm;"
    "avif;heic;exr;dpx;hdr;psd;pfm;raw;cr2;nef;arw;dng;ora";

}  // namespace

const std::string& MediaImportExtensions() {
    return kMediaImportExtensions;
}

bool IsMediaImportPath(const std::string& utf8_path) {
    // Extension after the last '.', unless a path separator follows
    // it (a dot inside a directory name is not an extension).
    const std::size_t dot = utf8_path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= utf8_path.size()) {
        return false;
    }
    const std::size_t sep = utf8_path.find_last_of("/\\");
    if (sep != std::string::npos && sep > dot) return false;
    const std::string ext =
        XER::Helper::String::ToLowerAscii(utf8_path.substr(dot + 1));
    // Token-compare, never substring — "xmp4" must not match "mp4".
    std::size_t pos = 0;
    while (true) {
        const std::size_t next = kMediaImportExtensions.find(';', pos);
        const std::string_view token =
            std::string_view(kMediaImportExtensions)
                .substr(pos, next == std::string::npos
                                 ? std::string::npos
                                 : next - pos);
        if (token == ext) return true;
        if (next == std::string::npos) return false;
        pos = next + 1;
    }
}

namespace Smoke {
namespace {

namespace Path = XER::Helper::Path;
namespace FFmpeg = XER::Bridge::FFmpeg;

// Hard cap for the entire run — a hung check must not block callers.
constexpr int kWatchdogSeconds = 15;

// Scratch area for the file-backed checks (store, autosave, catalog,
// presets, export). Distinct from the unit-test dir so a smoke run
// never collides with XER_VideoEditor_Tests fixtures.
std::filesystem::path SmokeTempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_video_editor_smoke";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

// Checked-in pixel baselines live in the SOURCE tree at
// tools/video_editor/tests/baselines — the exe sits under
// out/build/<preset>/bin/, so the dir is located by walking ancestors
// of the exe path (and the CWD as a fallback) until a directory that
// contains tools/video_editor/CMakeLists.txt turns up; that anchor
// keeps a stray same-named folder from fooling the search.
// XER_VE_BASELINE_DIR overrides discovery entirely. Empty = not found.
std::filesystem::path SmokeBaselineDir() {
    if (const char* env = std::getenv("XER_VE_BASELINE_DIR");
        env != nullptr && *env != '\0') {
        return Path::FromUtf8(env);
    }
    std::error_code ec;
    std::vector<std::filesystem::path> starts;
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0) {
        starts.push_back(std::filesystem::path(exe).parent_path());
    }
#elif defined(__linux__)
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) starts.push_back(self.parent_path());
#endif
    starts.push_back(std::filesystem::current_path(ec));
    for (const auto& start : starts) {
        std::filesystem::path dir = start;
        for (int up = 0; up < 8; ++up) {
            const auto ve = dir / "tools" / "video_editor";
            if (std::filesystem::exists(ve / "CMakeLists.txt", ec)) {
                return ve / "tests" / "baselines";
            }
            const auto parent = dir.parent_path();
            if (parent == dir) break;
            dir = parent;
        }
    }
    return {};
}

// PNG -> packed RGB8 via the FFmpeg image2 decode path — the same
// bridge FrameExporter wrote the file through, so encode/decode stay
// version-paired. Handles the packed layouts the PNG decoder emits
// (rgba/bgra/argb/abgr/rgb24/bgr24/gray); anything else (planar gbrp,
// 16-bit, palette) fails loudly so the diff never silently compares
// the wrong channels.
bool DecodePngToRgb8(const std::string& path,
                     std::vector<std::uint8_t>* rgb, int* width,
                     int* height, std::string* err) {
    std::vector<FFmpeg::VideoFrameInfo> frames;
    if (!FFmpeg::DecodeVideoFrames(path, 0, 1, &frames, err) ||
        frames.empty()) {
        if (err != nullptr && err->empty()) *err = "no frame decoded";
        return false;
    }
    const FFmpeg::VideoFrameInfo& f = frames.front();
    const std::string& fmt = f.pixel_format;
    int ch = 0, r = 0, g = 0, b = 0;   // channels + byte offsets
    if (fmt == "rgba")       { ch = 4; r = 0; g = 1; b = 2; }
    else if (fmt == "bgra")  { ch = 4; r = 2; g = 1; b = 0; }
    else if (fmt == "argb")  { ch = 4; r = 1; g = 2; b = 3; }
    else if (fmt == "abgr")  { ch = 4; r = 3; g = 2; b = 1; }
    else if (fmt == "rgb24" || fmt == "rgb")
        { ch = 3; r = 0; g = 1; b = 2; }
    else if (fmt == "bgr24" || fmt == "bgr")
        { ch = 3; r = 2; g = 1; b = 0; }
    else if (fmt == "gray" || fmt == "gray8") {
        ch = 1;   // r/g/b all read offset 0
    } else {
        if (err != nullptr) {
            *err = "unsupported decoded pixel format '" + fmt + "'";
        }
        return false;
    }
    const std::size_t pixels =
        static_cast<std::size_t>(f.width) * f.height;
    if (f.width <= 0 || f.height <= 0 ||
        f.data.size() < pixels * static_cast<std::size_t>(ch)) {
        if (err != nullptr) *err = "decoded buffer short for " + fmt;
        return false;
    }
    rgb->resize(pixels * 3);
    const std::uint8_t* src = f.data.data();
    for (std::size_t i = 0; i < pixels; ++i) {
        (*rgb)[3 * i + 0] = src[ch * i + r];
        (*rgb)[3 * i + 1] = src[ch * i + g];
        (*rgb)[3 * i + 2] = src[ch * i + b];
    }
    *width = f.width;
    *height = f.height;
    return true;
}

Clip MakeGeneratedClip(const std::string& id, int track, double start,
                       double duration, SourceKind kind,
                       const std::string& param) {
    Clip clip;
    clip.id = id;
    clip.track = track;
    clip.timeline_start = start;
    clip.duration = duration;
    clip.source.kind = kind;  // source_path stays empty — no media file
    clip.source.param = param;
    return clip;
}

// Small all-procedural project shared by the render-path checks:
// one generated bars clip on V1 plus an empty A1 — no decoder is ever
// opened, so these checks are truly headless.
Project MakeRenderProject(int width, int height, int fps_num) {
    Project project;
    project.width = width;
    project.height = height;
    project.fps_num = fps_num;
    project.fps_den = 1;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.tracks.push_back(Track{TrackKind::Audio, "A1"});
    project.clips.push_back(MakeGeneratedClip(
        "bars", 0, 0.0, 1.0, SourceKind::ColorBars, "smpte75"));
    return project;
}

// ---- checks ---------------------------------------------------------

// Assets-layer boot: app.yaml + V8 runtime.js + window.yaml. This is
// the same path RunGui takes before creating the window — it catches
// a broken/missing assets deploy, a dead JS engine, and malformed
// yaml without needing a visible window.
std::string CheckAppBoot() {
    std::string status;
    auto boot = AppRuntime::Boot(&status);
    if (!boot.ok) {
        return boot.error.empty() ? "assets boot failed"
                                  : "assets boot failed: " + boot.error;
    }
    if (boot.window.width <= 0 || boot.window.height <= 0) {
        return "window config has non-positive size";
    }
    // The BootResult owns the engine; clear the published pointer
    // before it dies so nothing dereferences a stale JsEngine.
    AppRuntime::SharedBootEngine() = nullptr;
    return {};
}

std::string CheckTimecode() {
    for (const double seconds : {0.0, 1.5, 61.04, 3661.5}) {
        const std::string text = FormatTimecode(seconds, 30, 1);
        if (text.size() != 11 || text[2] != ':' || text[5] != ':' ||
            text[8] != ':') {
            return "FormatTimecode produced '" + text + "'";
        }
        double parsed = -1.0;
        if (!ParseTimecode(text, 30, 1, &parsed)) {
            return "ParseTimecode rejected '" + text + "'";
        }
        const std::int64_t want = SecondsToFrames(seconds, 30, 1);
        const std::int64_t got = ParseToFrames(text, 30, 1);
        if (got != want) {
            return "round trip mismatch for '" + text + "'";
        }
    }
    if (ParseTimecode("not-a-timecode", 30, 1, nullptr) ||
        ParseTimecode("00:00:61:00", 30, 1, nullptr)) {
        return "accepted a malformed timecode";
    }
    return {};
}

std::string CheckSequencePresets() {
    const auto presets = BuiltinSequencePresets();
    if (presets.size() < 3) {
        return "too few builtin sequence presets";
    }
    for (const auto& preset : presets) {
        Project project;
        if (!ApplySequencePreset(preset, &project)) {
            return "ApplySequencePreset rejected '" + preset.name + "'";
        }
        if (project.width != preset.w || project.height != preset.h ||
            project.fps_num != preset.fps_num ||
            project.fps_den != preset.fps_den) {
            return "ApplySequencePreset misapplied '" + preset.name + "'";
        }
    }
    const SequencePreset found =
        FindPreset(presets.front().w, presets.front().h,
                   presets.front().fps_num, presets.front().fps_den);
    if (found.name.empty()) {
        return "FindPreset missed a builtin geometry";
    }
    return {};
}

std::string CheckTimelineModel() {
    Project project;
    project.width = 320;
    project.height = 180;
    project.fps_num = 30;
    project.fps_den = 1;
    std::string err;

    if (AddTrack(project, TrackKind::Video, "V1") != 0 ||
        AddTrack(project, TrackKind::Audio, "A1") != 1) {
        return "AddTrack returned unexpected indices";
    }

    const MediaBounds media{60.0, true};
    Clip clip;
    clip.id = "smoke-c1";
    clip.source_path = "clip.mp4";
    clip.track = 0;
    clip.duration = 5.0;
    if (!AddClip(project, clip, media, &err)) {
        return "AddClip rejected a valid clip: " + err;
    }

    // Invariant checks: overlap and audio-on-video must be rejected
    // transactionally (the project keeps its one clip).
    Clip overlap = clip;
    overlap.id = "smoke-c2";
    overlap.timeline_start = 2.0;
    if (AddClip(project, overlap, media, nullptr)) {
        return "AddClip accepted an overlapping clip";
    }
    if (project.clips.size() != 1) {
        return "rejected AddClip still mutated the project";
    }
    Clip audio_only = clip;
    audio_only.id = "smoke-c3";
    audio_only.timeline_start = 10.0;
    const MediaBounds audio_bounds{60.0, false};
    if (AddClip(project, audio_only, audio_bounds, nullptr)) {
        return "AddClip placed audio-only media on a video track";
    }

    if (!MoveClip(project, "smoke-c1", 10.0, media, &err) ||
        FindClip(project, "smoke-c1")->timeline_start != 10.0) {
        return "MoveClip failed: " + err;
    }
    if (!SplitClip(project, "smoke-c1", 12.0, "smoke-c1b", &err) ||
        project.clips.size() != 2) {
        return "SplitClip failed: " + err;
    }
    Marker marker;
    marker.time = 3.0;
    marker.name = "smoke";
    if (!AddMarker(project, marker, &err) ||
        !SetWorkArea(project, 1.0, 4.0, &err)) {
        return "marker/work-area op failed: " + err;
    }
    if (project.duration() != 15.0) {
        return "project duration mismatch after edits";
    }
    return {};
}

std::string CheckTimelineHistory() {
    Project project;
    TimelineHistory history(project);
    std::string err;

    if (history.AddTrack(TrackKind::Video, "V1") != 0) {
        return "history AddTrack failed";
    }
    Clip clip;
    clip.id = "smoke-h1";
    clip.source_path = "clip.mp4";
    clip.track = 0;
    clip.duration = 4.0;
    const MediaBounds media{30.0, true};
    if (!history.AddClip(clip, media, nullptr, &err)) {
        return "history AddClip failed: " + err;
    }
    if (!history.CanUndo() || history.UndoLabel().empty()) {
        return "history did not record the add";
    }
    if (!history.Undo() || !project.clips.empty()) {
        return "Undo did not remove the clip";
    }
    if (!history.CanRedo() || !history.Redo() ||
        project.clips.size() != 1) {
        return "Redo did not restore the clip";
    }
    return {};
}

std::string CheckSourceEdit() {
    // Headless three-point-edit path: marked source in/out resolves to
    // a Clip whose in_point/duration land through the model's
    // insert/overwrite ops — the same calls the source monitor's ,/.
    // keys dispatch (through TimelineHistory in the app).
    Project project;
    project.width = 320;
    project.height = 180;
    project.fps_num = 30;
    project.fps_den = 1;
    std::string err;
    if (AddTrack(project, TrackKind::Video, "V1") != 0) {
        return "AddTrack failed";
    }
    const MediaBounds media{20.0, true};
    Clip base;
    base.id = "base";
    base.source_path = "a.mp4";
    base.track = 0;
    base.timeline_start = 0.0;
    base.duration = 8.0;
    if (!AddClip(project, base, media, &err)) {
        return "AddClip failed: " + err;
    }

    // marks 2.0 -> 5.5 on a 20 s clip: resolves verbatim.
    SourceMarks marks;
    marks.in = 2.0;
    marks.out = 5.5;
    double src_in = 0.0, src_out = 0.0;
    if (!ResolveSourceRange(marks, 20.0, &src_in, &src_out) ||
        src_in != 2.0 || src_out != 5.5) {
        return "ResolveSourceRange mis-resolved marked range";
    }
    Clip edit = MakeSourceClip("b.mp4", 0, 10.0, src_in, src_out,
                               640, 360, 320, 180);
    if (edit.in_point != 2.0 || edit.duration != 3.5) {
        return "MakeSourceClip produced the wrong range";
    }
    edit.id = "edit";
    // Insert at t=10 (past the base clip): no straddler to split.
    if (!SourceEditAt(project, edit, media, true, &err)) {
        return "insert edit failed: " + err;
    }
    if (project.clips.size() != 2 ||
        FindClip(project, "edit") == nullptr) {
        return "inserted clip missing";
    }
    // Overwrite over the base clip's middle: [2,5.5) replaces its span.
    Clip over = MakeSourceClip("c.mp4", 0, 2.0, 3.0, 6.0,
                               640, 360, 320, 180);
    over.id = "over";
    if (!SourceEditAt(project, over, media, false, &err)) {
        return "overwrite edit failed: " + err;
    }
    const Clip* placed = FindClip(project, "over");
    if (placed == nullptr || placed->in_point != 3.0 ||
        placed->duration != 3.0) {
        return "overwrote clip has the wrong source range";
    }
    // Unmarked resolve falls back to the whole media duration.
    if (!ResolveSourceRange(SourceMarks{}, 7.0, &src_in, &src_out) ||
        src_in != 0.0 || src_out != 7.0) {
        return "unmarked range did not span the media";
    }
    // An inverted range (out <= in) must be rejected, not placed.
    SourceMarks bad;
    bad.in = 5.0;
    bad.out = 5.0;
    if (ResolveSourceRange(bad, 20.0, &src_in, &src_out)) {
        return "empty marked range resolved";
    }
    return {};
}

std::string CheckKeyboardMap() {
    const auto& shortcuts = EditorShortcuts();
    if (shortcuts.size() < 5) {
        return "shortcut table suspiciously small";
    }
    for (const auto& entry : shortcuts) {
        if (entry.group.empty() || entry.keys.empty() ||
            entry.action.empty()) {
            return "shortcut row has an empty field";
        }
    }
    return {};
}

std::string CheckExportPresets() {
    const auto presets = BuiltinPresets();
    if (presets.size() < 2) {
        return "too few builtin export presets";
    }
    ExportOptions options;
    ApplyPreset(presets.front(), &options);
    if (options.video_codec != presets.front().video_codec ||
        options.video_bit_rate != presets.front().video_bit_rate ||
        options.audio_codec != presets.front().audio_codec) {
        return "ApplyPreset did not stamp the preset fields";
    }

    const std::string path =
        Utf8Path(SmokeTempDir() / "export_presets.json");
    std::string err;
    if (!SavePresets(presets, path, &err)) {
        return "SavePresets failed: " + err;
    }
    std::vector<ExportPreset> loaded;
    if (!LoadPresets(path, &loaded, &err) ||
        loaded.size() != presets.size() ||
        loaded.front().name != presets.front().name) {
        return "preset round trip mismatch: " + err;
    }
    // Missing file is not an error by contract — first-run tolerance.
    std::vector<ExportPreset> none;
    if (!LoadPresets(path + ".missing", &none, nullptr) ||
        !none.empty()) {
        return "LoadPresets failed on a missing file";
    }
    return {};
}

std::string CheckGeneratedSource() {
    // BGRA: pure red parses to {0,0,255,255}; garbage falls back to
    // opaque black — both contracts are load-bearing for .xerproj
    // files in the wild.
    if (ParseMatteColor("#FF0000") !=
        std::array<std::uint8_t, 4>{0, 0, 255, 255}) {
        return "ParseMatteColor mis-parsed #FF0000";
    }
    if (ParseMatteColor("junk") !=
        std::array<std::uint8_t, 4>{0, 0, 0, 255}) {
        return "ParseMatteColor did not fall back to black";
    }

    video::Frame frame;
    Clip bars = MakeGeneratedClip("bars", 0, 0.0, 1.0,
                                  SourceKind::ColorBars, "smpte75");
    if (!GenerateSourceFrame(bars, 0.0, 160, 90, &frame) ||
        !frame.IsValid() || frame.Width() != 160 ||
        frame.Height() != 90) {
        return "ColorBars frame generation failed";
    }
    Clip title = MakeGeneratedClip("title", 0, 0.0, 1.0,
                                   SourceKind::Text, "Smoke | size=24");
    video::Frame text_frame;
    if (!GenerateSourceFrame(title, 0.0, 160, 90, &text_frame) ||
        !text_frame.IsValid()) {
        return "Text frame generation failed";
    }
    return {};
}

// Extended text-effect style set (text_style.h): a fully styled text
// clip must render non-empty pixels, the param serializer must round
// trip through the parser, and — when the build links Skia — the
// system font catalog must enumerate at least one family.
std::string CheckTextStyleExt() {
    Clip styled = MakeGeneratedClip(
        "styled", 0, 0.0, 1.0, SourceKind::Text,
        "Smoke Extended | font=Arial weight=700 italic=1 underline=1 "
        "size=28 tracking=1.5 align=center valign=middle x=4 y=2 "
        "w=300 h=80 radius=8 color=#FF8800FF opacity=0.95 outline=1 "
        "outline_color=#000000FF bg=#20202080 shadow=1 shadow_x=3 "
        "shadow_y=3 shadow_blur=2 shadow_color=#000000AA baseline=0");
    video::Frame styled_frame;
    if (!GenerateSourceFrame(styled, 0.0, 320, 180, &styled_frame) ||
        !styled_frame.IsValid()) {
        return "extended text clip failed to generate";
    }
    const bool any_pixels =
        std::any_of(styled_frame.Data(),
                    styled_frame.Data() + styled_frame.DataSize(),
                    [](std::uint8_t byte) { return byte != 0; });
    if (!any_pixels) {
        return "extended text clip rendered fully transparent";
    }
    // ...versus an empty text clip, which stays a 1x1 zero frame.
    Clip empty = MakeGeneratedClip("empty", 0, 0.0, 1.0,
                                   SourceKind::Text, "");
    video::Frame empty_frame;
    if (!GenerateSourceFrame(empty, 0.0, 320, 180, &empty_frame) ||
        !empty_frame.IsValid()) {
        return "empty text clip failed to generate";
    }
    if (empty_frame.DataSize() == styled_frame.DataSize() &&
        std::memcmp(empty_frame.Data(), styled_frame.Data(),
                    empty_frame.DataSize()) == 0) {
        return "styled text renders identically to empty text";
    }

    // Parse -> Serialize -> Parse round trip on every new field.
    const ClipTextStyle a = ParseClipTextStyle(styled.source.param);
    const ClipTextStyle b =
        ParseClipTextStyle(SerializeClipTextStyle(a));
    if (b.text != a.text || b.font_family != a.font_family ||
        b.font_weight != a.font_weight || b.italic != a.italic ||
        b.underline != a.underline || b.strike != a.strike ||
        b.size_px != a.size_px ||
        std::fabs(b.letter_spacing - a.letter_spacing) > 0.01f ||
        std::fabs(b.line_spacing - a.line_spacing) > 0.01f ||
        b.align != a.align || b.valign != a.valign ||
        std::fabs(b.pos_x - a.pos_x) > 0.01f ||
        std::fabs(b.pos_y - a.pos_y) > 0.01f ||
        std::fabs(b.box_w - a.box_w) > 0.01f ||
        std::fabs(b.box_h - a.box_h) > 0.01f ||
        std::fabs(b.corner_radius - a.corner_radius) > 0.01f ||
        std::fabs(b.rotate_deg - a.rotate_deg) > 0.01f ||
        std::fabs(b.baseline_px - a.baseline_px) > 0.01f ||
        b.color != a.color ||
        std::fabs(b.fill_opacity - a.fill_opacity) > 0.01f ||
        std::fabs(b.outline_px - a.outline_px) > 0.01f ||
        b.outline_color != a.outline_color || b.bg_color != a.bg_color ||
        b.shadow_explicit != a.shadow_explicit ||
        std::fabs(b.shadow_x - a.shadow_x) > 0.01f ||
        std::fabs(b.shadow_y - a.shadow_y) > 0.01f ||
        std::fabs(b.shadow_blur - a.shadow_blur) > 0.01f ||
        b.shadow_color != a.shadow_color) {
        return "text style round trip mismatch: " +
               SerializeClipTextStyle(b);
    }
    // A family name with spaces must survive the token grammar.
    const ClipTextStyle spaced = ParseClipTextStyle(
        "T | font=Segoe%20UI weight=400");
    if (spaced.font_family != "Segoe UI") {
        return "percent-encoded font family mis-decoded: " +
               spaced.font_family;
    }

    if (FontCatalog::Available() && FontCatalog::FamilyCount() <= 0) {
        return "font catalog enumerated zero families";
    }
    std::fprintf(stderr, "  font catalog: %d families (skia %s)\n",
                 FontCatalog::FamilyCount(),
                 FontCatalog::Available() ? "on" : "off");
    return {};
}

std::string CheckCompositorRender() {
    Project project = MakeRenderProject(160, 90, 15);
    Compositor compositor;
    std::string err;
    if (!compositor.Init(project, {}, &err)) {
        return "Compositor::Init failed: " + err;
    }
    std::vector<std::uint8_t> bgra;
    const bool rendered = compositor.RenderFrameAt(0.5, &bgra, &err);
    compositor.Shutdown();
    if (!rendered) {
        return "RenderFrameAt failed: " + err;
    }
    if (bgra.size() != static_cast<std::size_t>(160) * 90 * 4) {
        return "rendered buffer has the wrong size";
    }
    const bool any_pixels =
        std::any_of(bgra.begin(), bgra.end(),
                    [](std::uint8_t byte) { return byte != 0; });
    if (!any_pixels) {
        return "rendered frame is all zeros";
    }
    return {};
}

// The `--screenshot` pipeline in-process: a project round-trips
// through ProjectStore, the program frame renders through the
// headless Compositor and lands on disk as a PNG via
// video::FrameExporter — the file must carry the PNG magic plus an
// IHDR matching the canvas size.
std::string CheckScreenshotPng() {
    Project project = MakeRenderProject(160, 90, 15);
    const std::string proj_path =
        Utf8Path(SmokeTempDir() / "shot_project.xerproj");
    std::string err;
    if (!ProjectStore::Save(project, proj_path, &err)) {
        return "project save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(proj_path, &loaded, &err)) {
        return "project reload failed: " + err;
    }
    Compositor compositor;
    if (!compositor.Init(loaded, {}, &err)) {
        return "Compositor::Init failed: " + err;
    }
    std::vector<std::uint8_t> bgra;
    const bool rendered = compositor.RenderFrameAt(0.5, &bgra, &err);
    compositor.Shutdown();
    if (!rendered) {
        return "RenderFrameAt failed: " + err;
    }
    video::Frame frame;
    if (!frame.Allocate(160, 90, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    std::memcpy(frame.Data(), bgra.data(), bgra.size());
    const std::string png_path =
        Utf8Path(SmokeTempDir() / "shot_project.png");
    video::FrameExporter exporter;
    if (!exporter.ToFile(png_path, frame)) {
        return "PNG export failed";
    }
    std::ifstream in(Path::FromUtf8(png_path),
                     std::ios::binary | std::ios::ate);
    if (!in) {
        return "PNG missing after export";
    }
    const auto size = in.tellg();
    if (size < 64) {   // header alone is 33 bytes; bars need data too
        return "PNG suspiciously small (" +
               std::to_string(static_cast<long long>(size)) + " bytes)";
    }
    in.seekg(0);
    std::uint8_t header[24] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    static const std::uint8_t kMagic[8] = {0x89, 'P', 'N', 'G',
                                           0x0D, 0x0A, 0x1A, 0x0A};
    if (in.gcount() != static_cast<std::streamsize>(sizeof(header)) ||
        std::memcmp(header, kMagic, 8) != 0) {
        return "PNG magic mismatch";
    }
    // Bytes 8..11 = IHDR length (13 BE), 12..15 = "IHDR",
    // 16..23 = width/height big-endian.
    const auto be32 = [](const std::uint8_t* p) {
        return (static_cast<std::uint32_t>(p[0]) << 24) |
               (static_cast<std::uint32_t>(p[1]) << 16) |
               (static_cast<std::uint32_t>(p[2]) << 8) |
               static_cast<std::uint32_t>(p[3]);
    };
    if (be32(header + 8) != 13 ||
        std::memcmp(header + 12, "IHDR", 4) != 0 ||
        be32(header + 16) != 160 || be32(header + 20) != 90) {
        return "IHDR dimensions mismatch";
    }
    return {};
}

// `ui_screenshot_diff` — pixel-diff regression for the --screenshot
// pipeline. SCOPE NOTE (determinism): the diff covers the program-
// frame path only — ProjectStore -> Compositor -> FrameExporter PNG
// -> FFmpeg decode -> per-pixel compare. The full `--ui` chrome
// capture is deliberately out of scope: it lives exe-side (the
// core lib cannot link XER_UI_Core) and rasterizes fonts, panels and
// toasts whose output varies with the host's text stack — far too
// machine-dependent for a checked-in baseline. The program frame is
// fully procedural (SMPTE-75 generated bars on a fixed 1280x720
// canvas at t = 0.5, mid-clip), so it renders identically every run;
// the tolerance below absorbs codec/rasterizer-version drift only —
// content changes always exceed it.
//
// Baseline: tools/video_editor/tests/baselines/ui_default.png
// (located via SmokeBaselineDir; XER_VE_BASELINE_DIR overrides).
// A missing baseline is WRITTEN and the check passes — the first
// run establishes it; XER_VE_UPDATE_BASELINE=1 force-refreshes.
std::string CheckUiScreenshotDiff() {
    if (!FFmpeg::IsAvailable()) {
        return "ffmpeg bridge unavailable";
    }
    const std::filesystem::path baseline_dir = SmokeBaselineDir();
    if (baseline_dir.empty()) {
        return "tools/video_editor/tests/baselines not located "
               "(set XER_VE_BASELINE_DIR)";
    }
    const std::filesystem::path baseline =
        baseline_dir / "ui_default.png";

    // Same steps as RunScreenshotProgram in main.cc: save -> load ->
    // compositor -> RenderFrameAt -> PNG via FrameExporter.
    Project project = MakeRenderProject(1280, 720, 30);
    const std::string proj_path =
        Utf8Path(SmokeTempDir() / "diff_project.xerproj");
    std::string err;
    if (!ProjectStore::Save(project, proj_path, &err)) {
        return "project save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(proj_path, &loaded, &err)) {
        return "project reload failed: " + err;
    }
    Compositor compositor;
    if (!compositor.Init(loaded, {}, &err)) {
        return "Compositor::Init failed: " + err;
    }
    std::vector<std::uint8_t> bgra;
    const bool rendered = compositor.RenderFrameAt(0.5, &bgra, &err);
    compositor.Shutdown();
    if (!rendered) {
        return "RenderFrameAt failed: " + err;
    }
    const bool any_pixels =
        std::any_of(bgra.begin(), bgra.end(),
                    [](std::uint8_t byte) { return byte != 0; });
    if (!any_pixels) {
        return "rendered frame is all zeros";
    }
    video::Frame frame;
    if (!frame.Allocate(1280, 720, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    std::memcpy(frame.Data(), bgra.data(), bgra.size());
    const std::string shot_path =
        Utf8Path(SmokeTempDir() / "ui_default_actual.png");
    video::FrameExporter exporter;
    if (!exporter.ToFile(shot_path, frame)) {
        return "PNG export failed";
    }

    std::error_code ec;
    const bool have_baseline =
        std::filesystem::exists(baseline, ec) &&
        std::filesystem::file_size(baseline, ec) > 0;
    const char* update_env = std::getenv("XER_VE_UPDATE_BASELINE");
    const bool force_update =
        update_env != nullptr &&
        (std::string_view(update_env) == "1" ||
         std::string_view(update_env) == "true");
    if (!have_baseline || force_update) {
        std::filesystem::create_directories(baseline_dir, ec);
        if (!std::filesystem::copy_file(
                Path::FromUtf8(shot_path), baseline,
                std::filesystem::copy_options::overwrite_existing,
                ec)) {
            return "baseline write failed: " + ec.message();
        }
        std::fprintf(stderr,
                     "  ui_screenshot_diff: baseline %s -> %s\n",
                     force_update ? "refreshed" : "written",
                     Utf8Path(baseline).c_str());
        return {};
    }

    std::vector<std::uint8_t> actual_rgb, base_rgb;
    int aw = 0, ah = 0, bw = 0, bh = 0;
    if (!DecodePngToRgb8(shot_path, &actual_rgb, &aw, &ah, &err)) {
        return "actual decode failed: " + err;
    }
    if (!DecodePngToRgb8(Utf8Path(baseline), &base_rgb, &bw, &bh,
                         &err)) {
        return "baseline decode failed: " + err;
    }
    if (aw != bw || ah != bh) {
        return "size mismatch: actual " + std::to_string(aw) + "x" +
               std::to_string(ah) + " vs baseline " +
               std::to_string(bw) + "x" + std::to_string(bh);
    }
    // Tolerance: mean |delta| < 4.0 per channel-byte AND <1% of
    // pixels differ by >16 on any channel. PNG is lossless, so a
    // healthy run diffs at ~0; the slack only absorbs FFmpeg-version
    // pixel-format/dither drift, never content changes.
    std::uint64_t sum = 0;
    std::size_t hot = 0;
    const std::size_t pixels = static_cast<std::size_t>(aw) * ah;
    for (std::size_t i = 0; i < pixels; ++i) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(
                static_cast<int>(actual_rgb[3 * i + c]) -
                static_cast<int>(base_rgb[3 * i + c]));
            worst = std::max(worst, d);
            sum += static_cast<std::uint64_t>(d);
        }
        if (worst > 16) ++hot;
    }
    const double mean =
        static_cast<double>(sum) / static_cast<double>(pixels * 3);
    const double hot_frac = static_cast<double>(hot) / pixels;
    if (mean >= 4.0 || hot_frac >= 0.01) {
        char stats[96];
        std::snprintf(stats, sizeof(stats),
                      "mean=%.3f hot=%.2f%%", mean, hot_frac * 100.0);
        return std::string("diff too large: ") + stats +
               " (baseline " + Utf8Path(baseline) +
               "; set XER_VE_UPDATE_BASELINE=1 to refresh)";
    }
    return {};
}


// Curve easings + glass/lens kernels: new KeyEase curves must land on
// their endpoints (Bounce/Elastic/Back overshoot mid-span only); the
// refraction/DOF kernels must visibly move pixels on a bars frame.
std::string CheckCurvesAndLens() {
    Clip clip;
    clip.id = "curve";
    clip.duration = 4.0;
    clip.keyframes[KeyProp::Scale] = {
        {0.0, 1.0f, KeyEase::Bounce},
        {2.0, 2.0f, KeyEase::Linear},
        {4.0, 1.0f, KeyEase::Linear},
    };
    // Endpoints are exact regardless of the curve shape.
    if (std::fabs(EvaluateClipProperty(clip, KeyProp::Scale, 0.0, 1.0f) -
                  1.0f) > 1e-5f ||
        std::fabs(EvaluateClipProperty(clip, KeyProp::Scale, 2.0, 1.0f) -
                  2.0f) > 1e-5f) {
        return "eased curve missed its endpoints";
    }
    // Bounce mid-segment must sit between the key values.
    const float mid =
        EvaluateClipProperty(clip, KeyProp::Scale, 1.0, 1.0f);
    if (mid < 1.0f || mid > 2.0f) {
        return "bounce curve left the key range mid-segment";
    }
    for (KeyEase e : {KeyEase::Back, KeyEase::Elastic, KeyEase::ExpoOut,
                      KeyEase::EaseInCubic, KeyEase::EaseOutCubic}) {
        clip.keyframes[KeyProp::Scale].front().easing = e;
        const float end =
            EvaluateClipProperty(clip, KeyProp::Scale, 2.0 - 1e-9, 1.0f);
        if (std::fabs(end - 2.0f) > 0.05f) {
            return "easing does not settle on the segment end";
        }
    }

    Project project = MakeRenderProject(160, 90, 15);
    Clip* bars = &project.clips[0];
    bars->effects = {
        {"lens_sphere", {0.8f, 1.5f, 2.0f, 0.3f, 0.5f}},
        {"ribbed_glass", {16.0f, 3.0f, 0.0f, 0.3f}},
        {"bokeh_dof", {1.0f, 0.5f, 0.5f, 0.1f, 0.4f, 8.0f, 2.0f}},
    };
    Compositor compositor;
    std::string err;
    if (!compositor.Init(project, {}, &err)) {
        return "lens compositor init failed: " + err;
    }
    std::vector<std::uint8_t> fx_bgra;
    const bool rendered = compositor.RenderFrameAt(0.5, &fx_bgra, &err);
    compositor.Shutdown();
    if (!rendered) {
        return "lens frame render failed: " + err;
    }

    project.clips[0].effects.clear();
    Compositor plain;
    if (!plain.Init(project, {}, &err)) {
        return "plain compositor init failed: " + err;
    }
    std::vector<std::uint8_t> plain_bgra;
    const bool plain_ok = plain.RenderFrameAt(0.5, &plain_bgra, &err);
    plain.Shutdown();
    if (!plain_ok || plain_bgra.size() != fx_bgra.size()) {
        return "reference frame failed";
    }
    std::size_t moved = 0;
    for (std::size_t i = 0; i < fx_bgra.size(); i += 4) {
        if (fx_bgra[i] != plain_bgra[i] ||
            fx_bgra[i + 1] != plain_bgra[i + 1] ||
            fx_bgra[i + 2] != plain_bgra[i + 2]) {
            ++moved;
        }
    }
    if (moved < fx_bgra.size() / 4 / 10) {
        return "lens effects moved too few pixels";
    }
    return {};
}

// Preview-quality hint: Compositor::SetPreviewQuality(<1) runs the
// heavy glass/bokeh kernels on a downscaled frame and upsamples back
// — the result must stay close to (but not identical with) the full
// render, and at 1080p bokeh the preview pass must be measurably
// faster. Quality 1.0 must be byte-identical to the untouched
// default (the export/screenshot path depends on it).
std::string CheckPreviewQualityScale() {
    std::string err;

    // 1) Identity: quality 1.0 == never-set — byte for byte.
    {
        Project small = MakeRenderProject(160, 90, 15);
        small.clips[0].effects = {
            {"bokeh_dof", {1.0f, 0.5f, 0.5f, 0.1f, 0.4f, 8.0f, 2.0f}},
        };
        Compositor def;
        if (!def.Init(small, {}, &err)) {
            return "identity compositor init failed: " + err;
        }
        std::vector<std::uint8_t> ref;
        if (!def.RenderFrameAt(0.5, &ref, &err)) {
            return "identity reference render failed: " + err;
        }
        def.SetPreviewQuality(1.0f);
        std::vector<std::uint8_t> same;
        if (!def.RenderFrameAt(0.5, &same, &err)) {
            return "quality-1.0 render failed: " + err;
        }
        def.Shutdown();
        if (ref.size() != same.size() ||
            std::memcmp(ref.data(), same.data(), ref.size()) != 0) {
            return "SetPreviewQuality(1.0) is not pixel-identical";
        }
    }

    // 2) 1080p bokeh: preview render is faster and close-but-not-equal.
    {
        Project project = MakeRenderProject(1920, 1080, 30);
        project.clips[0].effects = {
            {"bokeh_dof", {1.0f, 0.5f, 0.5f, 0.12f, 0.3f, 18.0f, 3.0f}},
        };
        Compositor comp;
        if (!comp.Init(project, {}, &err)) {
            return "preview compositor init failed: " + err;
        }
        std::vector<std::uint8_t> full, half;
        const auto t0 = std::chrono::steady_clock::now();
        if (!comp.RenderFrameAt(0.5, &full, &err)) {
            return "full-quality render failed: " + err;
        }
        const double full_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0)
                .count();
        comp.SetPreviewQuality(0.5f);
        const auto t1 = std::chrono::steady_clock::now();
        if (!comp.RenderFrameAt(0.5, &half, &err)) {
            return "half-quality render failed: " + err;
        }
        const double half_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t1)
                .count();
        comp.Shutdown();
        if (half.size() != full.size()) {
            return "preview render changed the frame size";
        }
        // The reduced-res path must actually WIN — the whole point of
        // the hint. The ~4x expected margin makes this robust to
        // ordinary timing noise on a loaded machine.
        if (half_ms >= full_ms) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "preview path no faster (full %.0f ms vs "
                          "half %.0f ms)", full_ms, half_ms);
            return buf;
        }
        // Close but not identical: resampling is lossy, so the frames
        // must differ a little; if they matched exactly the hint
        // didn't engage, and a wild divergence means a broken kernel.
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < full.size(); ++i) {
            sum += static_cast<std::uint64_t>(std::abs(
                static_cast<int>(full[i]) - static_cast<int>(half[i])));
        }
        const double mean =
            static_cast<double>(sum) / static_cast<double>(full.size());
        std::fprintf(stderr,
                     "  preview_quality: full %.0f ms, half %.0f ms, "
                     "mean|diff| %.3f/byte\n",
                     full_ms, half_ms, mean);
        if (mean <= 0.001) {
            return "preview output identical to full — hint ignored?";
        }
        if (mean > 20.0) {
            return "preview output diverged too far (mean " +
                   std::to_string(mean) + ")";
        }
    }
    return {};
}

// JS fx: the catalog must list our stock scripts, and — when V8 is
// compiled in — a runtime must register + execute them. invert on a
// black pixel yields 255; bitcrush quantizes; smoothstep(0.5) = 0.5.
std::string CheckJsFx() {
    std::string status;
    AppRuntime::Boot(&status);   // fills SharedAssetsConfig
    const std::string fx_dir =
        (AppRuntime::SharedAssetsConfig().assets_dir /
         "scripts" / "fx").string();
    std::string err;
    if (!JsFxRuntime::LoadCatalog(fx_dir, &err)) {
        return "fx catalog load failed: " + err;
    }
    if (JsFxRuntime::VideoEffects().size() < 5 ||
        JsFxRuntime::AudioEffects().size() < 4 ||
        JsFxRuntime::ScriptTransitions().size() < 5) {
        return "fx catalog lists too few entries";
    }
    JsFxRuntime fx;
    if (!fx.EnsureLoaded(&err)) {
#if defined(XER_VE_HAS_V8)
        return "js fx runtime failed: " + err;
#else
        return {};   // no-V8 build — catalog-only check passes
#endif
    }
    if (!fx.HasVideoEffect("invert") ||
        !fx.HasAudioEffect("bitcrush") ||
        !fx.HasTransition("smoothstep")) {
        return "registered ids missing from the runtime";
    }
    video::Frame frame;
    if (!frame.Allocate(4, 4, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    if (!fx.ApplyVideo("invert", &frame, {}, 0.0, "bars", &err)) {
        return "invert apply failed: " + err;
    }
    if (frame.Data()[0] != 255 || frame.Data()[1] != 255 ||
        frame.Data()[2] != 255) {
        return "invert produced wrong pixels";
    }
    float pcm[4] = {0.1f, -0.1f, 0.9f, -0.9f};
    if (!fx.ProcessAudio("bitcrush", pcm, 2, 48000, {{0, 4.0f}}, 0.0,
                         "bars", &err)) {
        return "bitcrush process failed: " + err;
    }
    // 4-bit → 16 levels; 0.9 quantizes to a multiple of 1/16.
    const float q = pcm[2] * 16.0f;
    if (std::fabs(q - std::round(q)) > 0.01f) {
        return "bitcrush output not quantized";
    }
    const double w = fx.TransitionWeight("smoothstep", 0.5);
    if (std::fabs(w - 0.5) > 0.01) {
        return "smoothstep(0.5) != 0.5";
    }

    // ---- hardening: hostile scripts ----------------------------------
    // A private catalog + runtime exercises the failure paths the
    // stock scripts never hit: a throwing apply(), a runaway loop the
    // watchdog must terminate, and a stateful counter proving
    // ctx.clip_id scopes closure state per clip.
    const auto hostile_dir = SmokeTempDir() / "fx_hostile";
    {
        std::error_code ec;
        std::filesystem::create_directories(hostile_dir, ec);
        auto write = [&](const char* name, const char* text) {
            std::ofstream(hostile_dir / name,
                          std::ios::binary | std::ios::trunc)
                << text;
        };
        write("fx.json",
              "{\"video\":["
              "{\"id\":\"thrower\",\"file\":\"thrower.js\",\"params\":[]},"
              "{\"id\":\"runaway\",\"file\":\"runaway.js\",\"params\":[]},"
              "{\"id\":\"ok\",\"file\":\"ok.js\",\"params\":[]}],"
              "\"audio\":["
              "{\"id\":\"counter\",\"file\":\"counter.js\",\"params\":[]}],"
              "\"transitions\":[]}");
        write("thrower.js",
              "xerFxVideo({id:\"thrower\",apply(f,p,c){"
              "throw new Error(\"smoke boom\");}});\n");
        write("runaway.js",
              "xerFxVideo({id:\"runaway\",apply(f,p,c){"
              "while(true){}}});\n");
        write("ok.js",
              "xerFxVideo({id:\"ok\",apply(f,p,c){"
              "f.data[0]=42;}});\n");
        write("counter.js",
              "xerFxAudio(function(){const counts={};return{"
              "id:\"counter\",process(pcm,p,ctx){"
              "const k=ctx.clip_id||\"\";"
              "const c=(counts[k]=(counts[k]||0)+1);"
              "for(let i=0;i<pcm.length;++i)pcm[i]=c;}};}());\n");
    }
    if (!JsFxRuntime::LoadCatalog(hostile_dir.string(), &err)) {
        return "hostile fx catalog failed: " + err;
    }
    JsFxRuntime hostile;
    if (!hostile.EnsureLoaded(&err)) {
        return "hostile js runtime failed: " + err;
    }
    // (a) exception inside apply(): false + error text, no crash.
    video::Frame f2;
    if (!f2.Allocate(2, 2, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    if (hostile.ApplyVideo("thrower", &f2, {}, 0.0, "c1", &err)) {
        return "throwing apply() reported success";
    }
    if (err.find("boom") == std::string::npos) {
        return "apply() error text lost: " + err;
    }
    // (b) runaway apply(): the watchdog (500 ms budget) must kill it —
    // generous 4 s ceiling for CI jitter. Returns false (identity).
    const auto runaway_t0 = std::chrono::steady_clock::now();
    const bool runaway_ok =
        hostile.ApplyVideo("runaway", &f2, {}, 0.0, "c1", &err);
    const auto runaway_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - runaway_t0)
            .count();
    if (runaway_ok) {
        return "runaway apply() reported success";
    }
    if (runaway_ms > 4000) {
        return "runaway script not terminated (" +
               std::to_string(static_cast<long long>(runaway_ms)) +
               "ms)";
    }
    // The isolate must stay usable — termination is per-call.
    if (!hostile.ApplyVideo("ok", &f2, {}, 0.0, "c1", &err)) {
        return "runtime stayed broken after termination: " + err;
    }
    if (f2.Data()[0] != 42) {
        return "ok script did not run after termination";
    }
    // (c) ctx.clip_id scopes stateful audio closures: clipA counts
    // 1,2 while clipB starts its own counter at 1.
    float pcm_c[2] = {0.0f, 0.0f};
    auto count_for = [&](const std::string& clip) {
        pcm_c[0] = 0.0f;
        if (!hostile.ProcessAudio("counter", pcm_c, 1, 48000, {}, 0.0,
                                  clip, &err)) {
            return -1.0f;
        }
        return pcm_c[0];
    };
    if (count_for("clipA") != 1.0f || count_for("clipA") != 2.0f ||
        count_for("clipB") != 1.0f) {
        return "ctx.clip_id did not isolate per-clip script state";
    }
    return {};
}

std::string CheckAudioMixer() {
    // The bars clip owns no audio stream, so the correct output is
    // well-defined silence — the check exercises chunk production,
    // the s16-stereo contract, and the no-decoder path end to end.
    Project project = MakeRenderProject(160, 90, 15);
    AudioMixer mixer;
    AudioMixerOptions options;
    options.chunk_samples = 1024;
    std::string err;
    if (!mixer.Init(project, options, &err)) {
        return "AudioMixer::Init failed: " + err;
    }
    if (mixer.ChunkCount() < 1) {
        mixer.Shutdown();
        return "ChunkCount is zero for a 1 s project";
    }
    FFmpeg::AudioFrameInfo chunk;
    const bool got = mixer.GetChunk(0, &chunk, &err);
    mixer.Shutdown();
    if (!got) {
        return "GetChunk(0) failed: " + err;
    }
    if (chunk.sample_rate != 48000 || chunk.channels != 2 ||
        chunk.planar || chunk.sample_count <= 0) {
        return "chunk violates the s16-stereo contract";
    }
    if (chunk.data.size() !=
        static_cast<std::size_t>(chunk.sample_count) * 2 * 2) {
        return "chunk data size mismatch";
    }
    const bool silent =
        std::all_of(chunk.data.begin(), chunk.data.end(),
                    [](std::uint8_t byte) { return byte == 0; });
    if (!silent) {
        return "decoderless project produced non-silent audio";
    }
    return {};
}

// Synthetic waveform peaks shared by the audio-sync checks: a
// deterministic non-periodic energy pattern shifted by `shift`
// buckets (positive = the audio lands later in this file's own
// recording), noise floor off-pattern.
AudioPeaks MakeSyncTestPeaks(int buckets, int shift) {
    const auto pattern = [](int i) {
        std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u;
        h ^= h >> 16;
        return 0.05f + 0.9f * (h % 1024) / 1024.0f;
    };
    AudioPeaks peaks;
    peaks.channels = 1;
    peaks.samples_per_peak = 256;
    peaks.buckets = static_cast<std::size_t>(buckets);
    peaks.data.resize(static_cast<std::size_t>(buckets) * 2);
    for (int b = 0; b < buckets; ++b) {
        const int src = b - shift;
        const float v = (src >= 0 && src < buckets)
                            ? pattern(src)
                            : 0.02f;  // noise floor off-pattern
        peaks.data[2 * b] = -v;
        peaks.data[2 * b + 1] = v;
    }
    return peaks;
}

// Audio-sync lag math (audio_sync.h): synthetic AudioPeaks driven
// through the peaks-provider seam — no decoder, no media files. B is
// A delayed by kShift buckets: the plan must recover that lag and
// emit a NEGATIVE timeline shift of kShift bucket-seconds (the clip
// moves earlier because its take's audio sits later in its own
// recording). A leading copy proves the positive direction, and a
// noise-only clip proves the confidence gate.
std::string CheckAudioSync() {
    constexpr int kBuckets = 4096;   // ~21.8 s at 256/48k per bucket
    constexpr int kShift = 148;      // buckets; 37 envelope cells (x4)
    constexpr double kCell = 256.0 / 48000.0 * 4.0;  // env cell secs
    // Peak buckets for the reference pattern shifted by `shift`
    // (positive = audio lands later in this file's own recording).
    const auto make_peaks = [](int shift) {
        return MakeSyncTestPeaks(kBuckets, shift);
    };
    const AudioPeaks peaks_a = make_peaks(0);
    const AudioPeaks peaks_b = make_peaks(kShift);    // delayed copy
    const AudioPeaks peaks_d = make_peaks(-kShift);   // leading copy
    const AudioPeaks peaks_n = make_peaks(1 << 30);   // pure floor

    const auto provider = [&](const std::string& path,
                              AudioPeaks* out) -> bool {
        if (path == "cam_a.mp4") { *out = peaks_a; return true; }
        if (path == "cam_b.mp4") { *out = peaks_b; return true; }
        if (path == "cam_d.mp4") { *out = peaks_d; return true; }
        if (path == "cam_n.mp4") { *out = peaks_n; return true; }
        return false;
    };

    Project project;
    project.width = 320;
    project.height = 180;
    project.fps_num = 30;
    project.fps_den = 1;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.tracks.push_back(Track{TrackKind::Audio, "A1"});
    project.tracks.push_back(Track{TrackKind::Audio, "A2"});
    project.tracks.push_back(Track{TrackKind::Audio, "A3"});
    const auto media_clip = [](const std::string& id,
                               const std::string& path, int track,
                               double start, double dur) {
        Clip clip;
        clip.id = id;
        clip.source_path = path;
        clip.source.kind = SourceKind::Media;
        clip.track = track;
        clip.timeline_start = start;
        clip.duration = dur;
        return clip;
    };
    project.clips.push_back(
        media_clip("ref", "cam_a.mp4", 0, 0.0, 20.0));
    project.clips.push_back(
        media_clip("delayed", "cam_b.mp4", 1, 5.0, 20.0));
    project.clips.push_back(
        media_clip("leading", "cam_d.mp4", 2, 2.0, 20.0));
    project.clips.push_back(
        media_clip("noise", "cam_n.mp4", 3, 12.0, 10.0));

    AudioSyncReport report;
    std::string err;
    if (!ComputeAudioSyncPlanWithPeaks(project, "ref", provider,
                                       &report, &err)) {
        return "plan failed: " + err;
    }
    if (report.reference_clip_id != "ref" || report.status.empty()) {
        return "report missing reference/status";
    }
    const auto entry_for = [&](const std::string& id)
        -> const AudioSyncEntry* {
        for (const auto& e : report.entries) {
            if (e.clip_id == id) return &e;
        }
        return nullptr;
    };
    // lag cells = kShift/4 = 37 → delta = -37 * kCell ≈ -0.789 s.
    const double want_delta = -(kShift / 4.0) * kCell;
    const AudioSyncEntry* delayed = entry_for("delayed");
    if (delayed == nullptr || !delayed->matched) {
        return "delayed clip did not match";
    }
    if (std::fabs(delayed->delta_seconds - want_delta) > 1e-3 ||
        std::fabs(delayed->new_timeline_start -
                  (5.0 + want_delta)) > 1e-3) {
        return "delayed clip shift wrong: " +
               std::to_string(delayed->delta_seconds);
    }
    if (delayed->confidence < 0.5) {
        return "delayed clip confidence too low";
    }
    const AudioSyncEntry* leading = entry_for("leading");
    if (leading == nullptr || !leading->matched) {
        return "leading clip did not match";
    }
    if (std::fabs(leading->delta_seconds + want_delta) > 1e-3 ||
        std::fabs(leading->new_timeline_start -
                  (2.0 - want_delta)) > 1e-3) {
        return "leading clip shift wrong (sign): " +
               std::to_string(leading->delta_seconds);
    }
    const AudioSyncEntry* noise = entry_for("noise");
    if (noise == nullptr || noise->matched) {
        return "noise clip falsely matched";
    }
    return {};
}

std::string CheckProjectStore() {
    Project project;
    project.width = 320;
    project.height = 180;
    project.fps_num = 24;
    project.fps_den = 1;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.tracks.push_back(Track{TrackKind::Audio, "A1"});
    project.clips.push_back(MakeGeneratedClip(
        "matte", 0, 0.0, 2.0, SourceKind::ColorMatte, "#2244CC"));
    project.clips.push_back(MakeGeneratedClip(
        "bars", 0, 2.0, 1.5, SourceKind::ColorBars, "smpte75"));
    Marker marker;
    marker.time = 1.0;
    marker.name = "smoke-marker";
    project.markers.push_back(marker);
    project.work_in = 0.5;
    project.work_out = 3.0;
    project.transitions.push_back(Transition{
        "matte", "bars", Transition::Type::Crossfade, 0.5});

    const std::string path =
        Utf8Path(SmokeTempDir() / "smoke_project.xerproj");
    std::string err;
    if (!ProjectStore::Save(project, path, &err)) {
        return "Save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(path, &loaded, &err)) {
        return "Load failed: " + err;
    }
    if (loaded.width != project.width ||
        loaded.height != project.height ||
        loaded.fps_num != project.fps_num ||
        loaded.tracks.size() != project.tracks.size() ||
        loaded.clips.size() != project.clips.size() ||
        loaded.markers.size() != 1 ||
        loaded.work_in != project.work_in ||
        loaded.work_out != project.work_out) {
        return "round trip field mismatch";
    }
    const Clip* clip = FindClip(loaded, "bars");
    if (clip == nullptr || clip->source.kind != SourceKind::ColorBars ||
        clip->timeline_start != 2.0 || clip->duration != 1.5) {
        return "clip fields did not survive the round trip";
    }
    // Adjacent matte->bars means the transition must be preserved
    // rather than dropped by load-time self-healing.
    if (loaded.transitions.size() != 1 ||
        loaded.transitions.front().from_clip != "matte") {
        return "transition did not survive the round trip";
    }
    return {};
}

std::string CheckProjectAutosave() {
    const auto dir = SmokeTempDir();
    const std::string project_path = Utf8Path(dir / "autosave_me.xerproj");
    const std::string autosave_dir = Utf8Path(dir / "autosaves");

    Project project;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.clips.push_back(MakeGeneratedClip(
        "bars", 0, 0.0, 1.0, SourceKind::ColorBars, "smpte75"));

    AutosaveManager manager;
    std::string err;
    if (!manager.Init(project_path, autosave_dir, &err)) {
        return "Init failed: " + err;
    }
    manager.SetIntervalMs(0);  // always due
    manager.MarkDirty();
    if (!manager.MaybeAutosave(project, 1000, &err)) {
        return "MaybeAutosave failed: " + err;
    }
    if (manager.ListAutosaves().empty()) {
        return "no autosave slot was written";
    }
    Project recovered;
    if (!manager.LoadLatest(&recovered, &err)) {
        return "LoadLatest failed: " + err;
    }
    if (FindClip(recovered, "bars") == nullptr) {
        return "recovered project lost its clip";
    }
    return {};
}

std::string CheckMediaCatalog() {
    const std::string db_path =
        Utf8Path(SmokeTempDir() / "media_catalog.db");
    MediaCatalog catalog(db_path);
    std::string err;
    if (!catalog.Open(&err) || !catalog.IsOpen()) {
        return "Open failed: " + err;
    }
    err.clear();
    const auto entries = catalog.List(&err);
    if (!err.empty()) {
        catalog.Close();
        return "List failed: " + err;
    }
    if (!entries.empty()) {
        catalog.Close();
        return "fresh catalog is not empty";
    }
    // Negative paths must fail cleanly, not crash or fabricate rows.
    CatalogEntry entry;
    if (catalog.FindByPath("definitely/missing.mp4", &entry)) {
        catalog.Close();
        return "FindByPath found a phantom entry";
    }
    std::int64_t id = -1;
    if (catalog.Import("definitely/missing.mp4", &id, false)) {
        catalog.Close();
        return "Import accepted a missing file";
    }
    catalog.Close();
    return {};
}

// Records::RecentProjectsStore (records_store.h) — the SQLite recent-
// projects module behind the start screen. Exercises the full file
// contract: schema create, insert/touch recency (a re-touch must jump
// the rowid tiebreak even within one wall-clock second), sentinel
// metadata merge, Remove, the recency-preserving UpdateMeta, and — the
// actual point of a persistent store — data surviving Close/Open on
// the same file.
std::string CheckRecordsStore() {
    const std::string db_path =
        Utf8Path(SmokeTempDir() / "records_store.db");
    std::error_code ec;
    std::filesystem::remove(Path::FromUtf8(db_path), ec);  // fresh run
    const std::string dir = Utf8Path(SmokeTempDir());
    {
        Records::RecentProjectsStore store(db_path);
        std::string err;
        if (!store.Open(&err) || !store.IsOpen()) {
            return "Open failed: " + err;
        }
        if (!store.AddOrTouch(dir + "/alpha.xerproj", "Alpha", 12.5,
                              "", &err) ||
            !store.AddOrTouch(dir + "/beta.xerproj", {}, 0.0, "",
                              &err) ||
            !store.AddOrTouch(dir + "/gamma.xerproj", "Gamma", -1.0,
                              "posters/g.png", &err)) {
            return "AddOrTouch failed: " + err;
        }
        // Re-touch alpha — it must move to the front (fresh rowid
        // tiebreak inside the same timestamp second).
        if (!store.AddOrTouch(dir + "/alpha.xerproj", "Alpha", 12.5,
                              "", &err)) {
            return "re-touch failed: " + err;
        }
        std::vector<Records::RecentProject> rows = store.List(0, &err);
        if (!err.empty()) {
            return "List failed: " + err;
        }
        if (rows.size() != 3) {
            return "expected 3 rows, got " + std::to_string(rows.size());
        }
        if (!rows[0].path.ends_with("alpha.xerproj") ||
            !rows[1].path.ends_with("gamma.xerproj") ||
            !rows[2].path.ends_with("beta.xerproj")) {
            return "recency order broken after re-touch";
        }
        if (rows[0].display_name != "Alpha" ||
            rows[0].duration_seconds < 12.0) {
            return "metadata not stored on the touched row";
        }
        // beta had no display_name — the file-stem fallback applies.
        if (rows[2].display_name != "beta") {
            return "display_name stem fallback broken: '" +
                   rows[2].display_name + "'";
        }
        // gamma's -1 duration sentinel normalizes to 0; its poster
        // round-trips.
        if (rows[1].duration_seconds != 0.0 ||
            rows[1].poster_path != "posters/g.png") {
            return "sentinel/poster round-trip broken";
        }
        // UpdateMeta: fields merge, ordering is NOT perturbed.
        Records::RecentProject patch;
        patch.path = dir + "/beta.xerproj";
        patch.display_name = "Beta cut";
        patch.poster_path = "posters/b.png";
        if (!store.UpdateMeta(patch, &err)) {
            return "UpdateMeta failed: " + err;
        }
        rows = store.List(0, &err);
        if (rows.size() != 3 ||
            !rows[0].path.ends_with("alpha.xerproj") ||
            !rows[2].path.ends_with("beta.xerproj")) {
            return "UpdateMeta perturbed recency order";
        }
        if (rows[2].display_name != "Beta cut" ||
            rows[2].poster_path != "posters/b.png") {
            return "UpdateMeta fields did not merge";
        }
        if (!store.Remove(dir + "/gamma.xerproj", &err)) {
            return "Remove failed: " + err;
        }
        rows = store.List(0, &err);
        if (rows.size() != 2) {
            return "Remove left " + std::to_string(rows.size()) +
                   " rows";
        }
        store.Close();
    }
    // Second open on the same file — the persistence contract.
    Records::RecentProjectsStore store(db_path);
    std::string err;
    if (!store.Open(&err)) {
        return "reopen failed: " + err;
    }
    const std::vector<Records::RecentProject> rows = store.List(0, &err);
    if (rows.size() != 2 ||
        !rows[0].path.ends_with("alpha.xerproj") ||
        !rows[1].path.ends_with("beta.xerproj")) {
        store.Close();
        return "persistence across reopen broken";
    }
    if (rows[0].last_opened_unix <= 0) {
        store.Close();
        return "last_opened timestamp not persisted";
    }
    store.Close();
    std::filesystem::remove(Path::FromUtf8(db_path), ec);
    return {};
}

// Headless start-screen model (records_store.h display helpers — the
// part of the ProjectManager that can run without a window):
// FormatTimeAgo's band edges and MakeRecentTile's meta composition.
std::string CheckProjectManager() {
    constexpr std::int64_t kNow = 1'700'000'000;  // 2023-11-14
    const auto ago = [kNow](std::int64_t t) {
        return Records::FormatTimeAgo(t, kNow);
    };
    if (ago(kNow - 5) != "just now" ||
        ago(kNow + 60) != "just now") {      // clock skew clamps
        return "sub-minute band broken";
    }
    if (ago(kNow - 60) != "1 minute ago" ||
        ago(kNow - 120) != "2 minutes ago") {
        return "minutes band broken";
    }
    if (ago(kNow - 3600) != "1 hour ago" ||
        ago(kNow - 7200) != "2 hours ago") {
        return "hours band broken";
    }
    if (ago(kNow - 86400) != "yesterday" ||
        ago(kNow - 3 * 86400) != "3 days ago") {
        return "days band broken";
    }
    // Older than a week -> absolute "YYYY-MM-DD".
    const std::string old = ago(kNow - 30 * 86400);
    if (old.size() != 10 || old[4] != '-' || old[7] != '-') {
        return "absolute-date band broken: '" + old + "'";
    }
    if (!ago(0).empty()) {
        return "zero timestamp must yield an empty label";
    }

    Records::RecentProject rec;
    rec.path = "C:/work/promo_v2.xerproj";
    rec.last_opened_unix = kNow - 300;
    rec.duration_seconds = 65.0;   // -> "1:05"
    rec.poster_path = "posters/p.png";
    Records::RecentTile tile = Records::MakeRecentTile(rec, kNow);
    if (tile.title != "promo_v2") {
        return "tile stem fallback broken: '" + tile.title + "'";
    }
    if (tile.meta_line != "5 minutes ago \xc2\xb7 1:05") {
        return "tile meta composition broken: '" + tile.meta_line +
               "'";
    }
    if (tile.poster_path != "posters/p.png") {
        return "tile poster not carried";
    }
    rec.display_name = "Promo v2";
    rec.duration_seconds = 0.0;
    tile = Records::MakeRecentTile(rec, kNow);
    if (tile.title != "Promo v2" ||
        tile.meta_line != "5 minutes ago") {
        return "tile name/no-duration composition broken";
    }
    return {};
}

// IsAttachedPictureStream() — the exported seam ProbeVideo applies to
// every "video"-typed stream to decide whether it is embedded cover
// art (never the primary video) or real footage. Truth-table the real
// contract: attached_pic disposition, "cover" metadata, any picture
// stream in an audio-primary container, and a 1-2 frame image-codec
// stream beside audio all classify as art — while MJPEG camera
// footage with an unknown frame count and lone stills must stay video.
std::string CheckProbeClassification() {
    namespace CV = XER::Content::Video;
    const std::string mp4 = "mov,mp4,m4a,3gp,3g2,mj2";

    FFmpeg::StreamInfo base;
    base.media_type = "video";
    base.codec_name = "mjpeg";

    // attached_pic disposition beside audio -> art.
    FFmpeg::StreamInfo pic = base;
    pic.attached_pic = true;
    pic.frame_count = 1;
    if (!CV::IsAttachedPictureStream(pic, true, mp4)) {
        return "attached_pic stream not classified as art";
    }
    // "cover" metadata marker -> art even without an audio stream
    // (the metadata check runs before the audio gate).
    FFmpeg::StreamInfo meta = base;
    meta.metadata.push_back({"comment", "Cover (front)"});
    if (!CV::IsAttachedPictureStream(meta, false, mp4)) {
        return "cover metadata not classified as art";
    }
    // mjpeg inside an audio-primary container (mp3) with 1-2 frames
    // next to audio -> art even with no disposition/metadata tells.
    FFmpeg::StreamInfo mp3_art = base;
    for (const std::int64_t frames : {1, 2}) {
        mp3_art.frame_count = frames;
        if (!CV::IsAttachedPictureStream(mp3_art, true, "mp3")) {
            return "mp3 picture stream (" + std::to_string(frames) +
                   " frames) not classified as art";
        }
    }
    // MJPEG camera footage: image codec but frame_count unknown (0)
    // in a general container -> stays video.
    FFmpeg::StreamInfo camera = base;  // frame_count stays 0
    if (CV::IsAttachedPictureStream(camera, true, mp4)) {
        return "uncounted MJPEG footage misclassified as art";
    }
    // Many image-codec frames -> a real video, not a cover.
    camera.frame_count = 300;
    if (CV::IsAttachedPictureStream(camera, true, mp4)) {
        return "300-frame MJPEG stream misclassified as art";
    }
    // A 1-2 frame image-codec stream beside audio in a general
    // container -> art by the frame-count heuristic.
    FFmpeg::StreamInfo still = base;
    still.codec_name = "png";
    still.frame_count = 1;
    if (!CV::IsAttachedPictureStream(still, true, mp4)) {
        return "1-frame image stream beside audio not classified as art";
    }
    // The same lone still with no audio anywhere keeps counting as
    // video — the audio gate keeps single pictures usable.
    if (CV::IsAttachedPictureStream(still, false, mp4)) {
        return "lone still without audio misclassified as art";
    }
    // A real video codec never classifies on frame count alone.
    FFmpeg::StreamInfo h264 = base;
    h264.codec_name = "h264";
    h264.frame_count = 1;
    if (CV::IsAttachedPictureStream(h264, true, mp4)) {
        return "1-frame h264 stream misclassified as art";
    }
    return {};
}

// PosterCache (poster_cache.h) byte-budgeted LRU: an oversized frame
// is never retained, many small frames evict LRU-first until
// BytesUsed() fits the capacity, and a Get() hit promotes the key so
// it survives further evictions. UI-free — frames only, no decoder.
std::string CheckPosterCacheLru() {
    constexpr std::size_t kCapacity = 1u * 1024u * 1024u;  // 1 MiB
    PosterCache cache(kCapacity);

    // Oversized: a 2 MiB frame exceeds the whole budget -> the cache
    // drops it immediately instead of keeping it.
    video::Frame big;
    if (!big.Allocate(512, 1024, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    cache.Put("big.mp4", 0, std::move(big));
    if (cache.Get("big.mp4", 0) != nullptr) {
        return "oversized frame was retained";
    }

    // 64 KiB frames: 16 fit exactly in 1 MiB. One hot key plus 15 cold
    // keys, touch the hot one, then churn 8 more — the promoted key
    // must survive while the oldest cold keys evict.
    video::Frame small;
    if (!small.Allocate(128, 128, video::PixelFormat::BGRA8)) {
        return "frame allocate failed";
    }
    cache.Put("hot.mp4", 0, small);
    for (int i = 0; i < 15; ++i) {
        cache.Put("clip.mp4", i, small);
    }
    if (cache.Get("hot.mp4", 0) == nullptr) {
        return "hot key missing before the churn";
    }
    for (int i = 0; i < 8; ++i) {
        cache.Put("filler.mp4", i, small);
    }
    const PosterCache::Stats stats = cache.GetStats();
    if (stats.bytes > stats.capacity_bytes) {
        return "BytesUsed() exceeds capacity";
    }
    if (stats.evictions == 0) {
        return "no evictions recorded under pressure";
    }
    if (cache.Get("hot.mp4", 0) == nullptr) {
        return "hot key evicted despite Get() promotion";
    }
    return {};
}

// Deployed assets audit: the app boots off <exe>/assets/app.yaml, the
// icon pack rasterizes SVGs resolved through icons.yaml, and the
// .xexrj extension host scans assets/extensions/. Everything the
// runtime loads must actually be deployed — missing entries are
// reported in the error string.
std::string CheckAssetsIcons() {
    std::string status;
    auto boot = AppRuntime::Boot(&status);  // resolves assets_dir
    // Same hygiene as app_boot: drop the published engine pointer
    // before this result dies.
    AppRuntime::SharedBootEngine() = nullptr;
    const auto& cfg = AppRuntime::SharedAssetsConfig();
    if (cfg.assets_dir.empty()) {
        return "assets dir not deployed (app.yaml not found)";
    }
    std::error_code ec;
    std::vector<std::string> missing;
    if (!std::filesystem::exists(cfg.assets_dir / "app.yaml", ec)) {
        missing.push_back("app.yaml");
    }
    if (!std::filesystem::exists(cfg.assets_dir / "icons.yaml", ec)) {
        missing.push_back("icons.yaml");
    }
    // Icons dir: the path Boot resolved, falling back to the
    // conventional assets/icons layout.
    const std::filesystem::path icons_dir =
        !cfg.icons_dir.empty() ? cfg.icons_dir
                               : cfg.assets_dir / "icons";
    if (!std::filesystem::is_directory(icons_dir, ec)) {
        missing.push_back("icons/");
    } else {
        int svg_count = 0;
        for (const auto& de :
             std::filesystem::directory_iterator(icons_dir, ec)) {
            if (de.path().extension() == ".svg") {
                ++svg_count;
            }
        }
        if (svg_count == 0) {
            missing.push_back("icons/*.svg");
        }
        // Every icons.yaml mapping must resolve under the icons dir —
        // IconPack::Get() builds its path exactly this way.
        for (const auto& [name, file] : cfg.icon_map) {
            if (!std::filesystem::exists(icons_dir / file, ec)) {
                missing.push_back("icon:" + name + "->" + file);
            }
        }
    }
    // .xexrj extension packages — the ext host rescans this dir.
    const std::filesystem::path ext_dir = cfg.assets_dir / "extensions";
    bool has_xexrj = false;
    if (std::filesystem::is_directory(ext_dir, ec)) {
        for (const auto& de :
             std::filesystem::directory_iterator(ext_dir, ec)) {
            if (de.path().extension() == ".xexrj") {
                has_xexrj = true;
                break;
            }
        }
    }
    if (!has_xexrj) {
        missing.push_back("extensions/*.xexrj");
    }
    if (!missing.empty()) {
        std::string reason = "missing:";
        for (const std::string& entry : missing) {
            reason += " " + entry;
        }
        return reason;
    }
    return {};
}

// End-to-end: compositor -> libx264 -> mp4 mux via the real FFmpeg
// bridge, all on procedural clips — no media fixture required. Kept
// last since it is the slowest check (~1 s typical).
std::string CheckExportRender() {
    if (!FFmpeg::IsAvailable()) {
        return "ffmpeg bridge unavailable";
    }
    const auto output = SmokeTempDir() / "smoke_export.mp4";
    {
        std::error_code ec;
        std::filesystem::remove(output, ec);
    } 
    ExportOptions options;
    options.output_path = Utf8Path(output);
    options.video_bit_rate = 200000;

    ExportEngine engine;
    std::string err;
    if (!engine.Start(MakeRenderProject(64, 64, 15), options, {},
                      &err)) {
        return "Start failed: " + err;
    }
    engine.Wait();
    if (engine.Status() != ExportStatus::kDone) {
        return "export ended in state " +
               std::to_string(static_cast<int>(engine.Status())) +
               ": " + engine.Error();
    }
    std::error_code ec;
    if (!std::filesystem::exists(output, ec) ||
        std::filesystem::file_size(output, ec) == 0) {
        return "export produced no output file";
    }
    return {};
}

// Out-of-process media worker (media_worker_client.h): the client
// spawns the dedicated XERVideoLoader.exe sitting NEXT TO this exe
// when present — dev builds without it fall back to self-spawning
// `--media-worker`. Either way the worker must answer a ping; the
// dedicated case additionally checks the role string to prove the
// named binary (not the fallback, not a PATH lookalike) replied.
std::string CheckMediaWorker() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    const bool dedicated =
        std::filesystem::exists(bin_dir / "XERVideoLoader.exe", ec);
    auto& client = MediaWorkerClient::Shared();
    if (!client.Available()) {
        return "media worker spawn failed";
    }
    std::string role;
    if (!client.Ping(&role)) {
        return "media worker ping failed";
    }
    if (dedicated && role != "XERVideoLoader") {
        return "XERVideoLoader.exe present but worker role is '" +
               role + "'";
    }
    if (!dedicated && role != "media-worker") {
        return "unexpected worker role '" + role + "'";
    }
    return {};
#else
    return {};   // the subprocess client is Windows-only
#endif
}

// XERSyncMedia.exe (worker_pipe.h): the audio-sync plan computed
// INSIDE the named worker — the same synthetic-peaks math as the
// audio_sync check, but the peaks ride the sync_plan request's
// injection seam so no decoder runs. Asserts the dedicated exe's
// role string; when the exe is absent the documented in-process
// fallback is exercised instead.
std::string CheckSyncMediaWorker() {
#if defined(_WIN32)
    constexpr int kBuckets = 4096;
    constexpr int kShift = 148;
    constexpr double kCell = 256.0 / 48000.0 * 4.0;
    const AudioPeaks peaks_a = MakeSyncTestPeaks(kBuckets, 0);
    const AudioPeaks peaks_b = MakeSyncTestPeaks(kBuckets, kShift);

    // The minimal in-process sanity proof used when the worker exe is
    // not beside this binary (dev shells, partial builds).
    auto in_process = [&]() -> std::string {
        Project project;
        project.tracks.push_back(Track{TrackKind::Audio, "A1"});
        const auto media_clip = [](const std::string& id,
                                   const std::string& path,
                                   double start) {
            Clip clip;
            clip.id = id;
            clip.source_path = path;
            clip.source.kind = SourceKind::Media;
            clip.track = 0;
            clip.timeline_start = start;
            clip.duration = 20.0;
            return clip;
        };
        project.clips.push_back(media_clip("ref", "cam_a.mp4", 0.0));
        project.clips.push_back(
            media_clip("delayed", "cam_b.mp4", 5.0));
        const auto provider = [&](const std::string& path,
                                  AudioPeaks* out) -> bool {
            *out = path == "cam_b.mp4" ? peaks_b : peaks_a;
            return true;
        };
        AudioSyncReport report;
        std::string err;
        if (!ComputeAudioSyncPlanWithPeaks(project, "ref", provider,
                                           &report, &err)) {
            return "in-process fallback plan failed: " + err;
        }
        return {};
    };

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const bool dedicated = std::filesystem::exists(
        std::filesystem::path(exe).parent_path() /
        "XERSyncMedia.exe");
    WorkerPipe& pipe = SyncMediaPipe();
    if (!dedicated || !pipe.Available()) {
        return in_process();   // fallback tolerated
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        return "sync worker ping failed";
    }
    if (role != "XERSyncMedia") {
        return "unexpected sync worker role '" + role + "'";
    }

    MediaWorker::SyncPlanRequestData data;
    data.ref_index = 0;
    const auto clip = [](const char* id, const char* path,
                         double start, const AudioPeaks& peaks) {
        MediaWorker::SyncPlanClip c;
        c.clip_id = id;
        c.path = path;
        c.start = start;
        c.out_point = 20.0;
        c.rate = 1.0;
        c.has_peaks = true;
        c.peaks = peaks;
        return c;
    };
    data.clips.push_back(clip("ref", "cam_a.mp4", 0.0, peaks_a));
    data.clips.push_back(
        clip("delayed", "cam_b.mp4", 5.0, peaks_b));
    const std::uint64_t id = pipe.NextId();
    std::string reply;
    if (!pipe.RoundTrip(
            id, MediaWorker::EncodeSyncPlanRequest(id, data), 60000,
            &reply)) {
        return "sync_plan request got no reply";
    }
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!MediaWorker::DecodeReplyEnvelope(reply, &rid, &ok, &err)) {
        return "sync_plan reply is malformed";
    }
    if (!ok) {
        return "sync_plan rejected: " + err;
    }
    MediaWorker::SyncPlanResult result;
    if (!MediaWorker::DecodeSyncPlanReply(reply, &rid, &result)) {
        return "sync_plan reply payload failed to decode";
    }
    if (result.reference_clip_id != "ref") {
        return "sync_plan picked the wrong reference";
    }
    const MediaWorker::SyncPlanResultEntry* delayed = nullptr;
    for (const auto& e : result.entries) {
        if (e.clip_id == "delayed") delayed = &e;
    }
    if (delayed == nullptr || !delayed->matched) {
        return "worker did not match the delayed clip";
    }
    // lag cells = kShift/4 = 37 → delta = -37 * kCell ≈ -0.789 s.
    const double want_delta = -(kShift / 4.0) * kCell;
    if (std::fabs(delayed->delta_seconds - want_delta) > 1e-3) {
        return "worker shift has the wrong sign/magnitude: " +
               std::to_string(delayed->delta_seconds);
    }
    if (delayed->confidence < 0.5) {
        return "worker confidence too low";
    }
    return {};
#else
    return {};
#endif
}

// XERFxScript.exe (worker_pipe.h): the JS fx host answers over the
// pipe — fx_list must mirror the deployed catalog, fx_audio must
// actually run a script (bitcrush on a sine block quantizes), and
// fx_transition must return the smoothstep scalar. Asserts the role;
// a missing exe or a dead engine falls back to pass — js_fx already
// proves the in-process runtime.
std::string CheckFxScriptWorker() {
#if defined(_WIN32)
    std::string status;
    AppRuntime::Boot(&status);   // fills SharedAssetsConfig
    const std::string fx_dir =
        (AppRuntime::SharedAssetsConfig().assets_dir / "scripts" /
         "fx")
            .string();
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const bool dedicated = std::filesystem::exists(
        std::filesystem::path(exe).parent_path() / "XERFxScript.exe");
    WorkerPipe& pipe = FxScriptPipe();
    if (!dedicated || !pipe.Available()) {
        return {};   // fallback tolerated
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        return "fx worker ping failed";
    }
    if (role != "XERFxScript") {
        return "unexpected fx worker role '" + role + "'";
    }

    const auto round_trip =
        [&](const std::function<std::string(std::uint64_t)>& encode,
            std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            return pipe.RoundTrip(id, encode(id), 30000, reply);
        };
    // Tolerate engine-side rejections (V8-less worker, undeployed
    // catalog) but not transport silence — the worker answered ping.
    const auto envelope_ok = [](const std::string& reply,
                                std::string* err) -> int {
        std::uint64_t id = 0;
        bool ok = false;
        if (!MediaWorker::DecodeReplyEnvelope(reply, &id, &ok,
                                              err)) {
            return -1;   // malformed
        }
        return ok ? 1 : 0;
    };

    // fx_list — pure JSON; a live worker must always answer it.
    std::string reply;
    if (!round_trip(
            [&](std::uint64_t id) {
                return MediaWorker::EncodeFxListRequest(id, fx_dir);
            },
            &reply)) {
        return "fx_list got no reply";
    }
    MediaWorker::FxListReply list;
    std::uint64_t rid = 0;
    if (!MediaWorker::DecodeFxListReply(reply, &rid, &list)) {
        return "fx_list reply failed to decode";
    }
    const auto has_id = [](const std::vector<MediaWorker::FxListItem>&
                               items,
                           const char* id) {
        for (const auto& item : items) {
            if (item.id == id) return true;
        }
        return false;
    };
    if (list.video.size() < 5 || list.audio.size() < 4 ||
        list.transitions.size() < 5 ||
        !has_id(list.video, "invert") ||
        !has_id(list.audio, "bitcrush") ||
        !has_id(list.transitions, "smoothstep")) {
        return "fx_list does not mirror the deployed catalog";
    }

    // fx_audio — bitcrush (4 bits) on a stereo sine block must come
    // back quantized.
    constexpr int kFrames = 1024;
    std::vector<float> pcm(static_cast<std::size_t>(kFrames) * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float s = 0.8f * std::sin(
            2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        pcm[2 * i] = s;
        pcm[2 * i + 1] = s;
    }
    const std::map<std::uint32_t, float> params{{0, 4.0f}};
    std::string audio_reply;
    if (!round_trip(
            [&](std::uint64_t id) {
                return MediaWorker::EncodeFxAudioRequest(
                    id, fx_dir, "bitcrush", "smoke-clip", pcm.data(),
                    kFrames, 2, 48000, params, 0.0);
            },
            &audio_reply)) {
        return {};   // transport died — tolerated fallback
    }
    std::string err;
    const int audio_state = envelope_ok(audio_reply, &err);
    if (audio_state < 0) {
        return "fx_audio reply is malformed";
    }
    if (audio_state == 0) {
        return {};   // engine unavailable (no V8/catalog) — tolerated
    }
    std::vector<float> out_pcm;
    if (!MediaWorker::DecodeFxAudioReply(audio_reply, &rid,
                                         &out_pcm) ||
        out_pcm.size() != pcm.size()) {
        return "fx_audio payload failed to decode";
    }
    // bits=4 → 16 levels → every sample is a multiple of 1/16, and a
    // sine quantized to 16 levels differs from the input almost
    // everywhere (proving the script actually ran).
    int unquantized = 0;
    int changed = 0;
    for (std::size_t i = 0; i < out_pcm.size(); ++i) {
        const float q16 = out_pcm[i] * 16.0f;
        if (std::fabs(q16 - std::round(q16)) > 0.02f) {
            ++unquantized;
        }
        if (std::fabs(out_pcm[i] - pcm[i]) > 1e-4f) {
            ++changed;
        }
    }
    if (unquantized > 0) {
        return "fx_audio output is not bitcrush-quantized";
    }
    if (changed < static_cast<int>(out_pcm.size() / 2)) {
        return "fx_audio left the block unchanged";
    }

    // fx_transition — smoothstep(0.5) == 0.5.
    std::string trans_reply;
    if (!round_trip(
            [&](std::uint64_t id) {
                return MediaWorker::EncodeFxTransitionRequest(
                    id, fx_dir, "smoothstep", 0.5);
            },
            &trans_reply)) {
        return {};   // tolerated
    }
    const int trans_state = envelope_ok(trans_reply, &err);
    if (trans_state < 0) {
        return "fx_transition reply is malformed";
    }
    if (trans_state == 0) {
        return {};   // engine unavailable — tolerated
    }
    double weight = -1.0;
    if (!MediaWorker::DecodeFxTransitionReply(trans_reply, &rid,
                                              &weight) ||
        std::fabs(weight - 0.5) > 0.01) {
        return "fx_transition returned the wrong weight";
    }
    return {};
#else
    return {};
#endif
}

// `generated_layers` — the "Add Color Matte / Color Bars / Text"
// flow headless end to end: clips are created through the MODEL ops
// (AddTrack + AddClip with unbounded video bounds — the same calls
// the app's AddGeneratedClipAt issues through TimelineHistory),
// composited through the real Compositor (each kind must produce its
// signature pixels — proving no clip took a decoder path), then the
// project round-trips through ProjectStore so source.kind +
// source.param + the Clip::name layer label all survive save/load.
std::string CheckGeneratedLayers() {
    Project project;
    project.width = 160;
    project.height = 90;
    project.fps_num = 30;
    project.fps_den = 1;
    std::string err;
    if (AddTrack(project, TrackKind::Video, "V1") != 0 ||
        AddTrack(project, TrackKind::Video, "V2") != 1) {
        return "AddTrack failed";
    }

    const MediaBounds gen_bounds{0.0, true};   // unbounded, has video
    Clip matte = MakeGeneratedClip("matte", 0, 0.0, 6.0,
                                   SourceKind::ColorMatte, "#3366CC");
    matte.name = "Backdrop";
    Clip bars = MakeGeneratedClip("bars", 1, 0.0, 3.0,
                                  SourceKind::ColorBars, "smpte75");
    Clip text = MakeGeneratedClip("title", 1, 3.0, 3.0,
                                  SourceKind::Text, "Smoke | size=24");
    text.name = "Smoke Title";
    for (const Clip* clip : {&matte, &bars, &text}) {
        if (!AddClip(project, *clip, gen_bounds, &err)) {
            return "AddClip rejected a generated clip: " + err;
        }
    }

    Compositor compositor;
    if (!compositor.Init(project, {}, &err)) {
        return "Compositor::Init failed: " + err;
    }
    std::vector<std::uint8_t> bgra;
    const auto pixel = [](const std::vector<std::uint8_t>& buf,
                          int x, int y) {
        const auto* p =
            buf.data() + (static_cast<std::size_t>(y) * 160 + x) * 4u;
        return std::array<std::uint8_t, 4>{p[0], p[1], p[2], p[3]};
    };

    // t=1.5: V2 SMPTE-75 bars composite over the V1 matte — the first
    // column is 191 gray, the last is blue (BGRA byte order).
    if (!compositor.RenderFrameAt(1.5, &bgra, &err)) {
        compositor.Shutdown();
        return "RenderFrameAt(bars) failed: " + err;
    }
    if (bgra.size() != static_cast<std::size_t>(160) * 90 * 4) {
        compositor.Shutdown();
        return "rendered buffer has the wrong size";
    }
    if (pixel(bgra, 10, 45) !=
            std::array<std::uint8_t, 4>{191, 191, 191, 255} ||
        pixel(bgra, 159, 45) !=
            std::array<std::uint8_t, 4>{191, 0, 0, 255}) {
        compositor.Shutdown();
        return "ColorBars pixels wrong at t=1.5";
    }

    // t=4.5: V2 text composites over the matte — a bottom-right pixel
    // is the matte color (#3366CC -> BGRA CC,66,33 — hex parse proven
    // end to end) while a bright glyph pixel must exist somewhere
    // (the text actually drew).
    if (!compositor.RenderFrameAt(4.5, &bgra, &err)) {
        compositor.Shutdown();
        return "RenderFrameAt(text) failed: " + err;
    }
    compositor.Shutdown();
    if (pixel(bgra, 159, 89) !=
        std::array<std::uint8_t, 4>{0xCC, 0x66, 0x33, 0xFF}) {
        return "matte underlay pixel wrong at t=4.5";
    }
    const bool any_glyph = std::any_of(
        bgra.begin(), bgra.end(),
        [](std::uint8_t byte) { return byte >= 200; });
    if (!any_glyph) {
        return "text layer produced no glyph pixels";
    }

    // Persistence: kind + param + name + placement all survive a
    // .xerproj round trip — the LAYERS panel reads Clip::name, so a
    // silent drop would strip every user-facing label on reload.
    const std::string path =
        Utf8Path(SmokeTempDir() / "generated_layers.xerproj");
    if (!ProjectStore::Save(project, path, &err)) {
        return "Save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(path, &loaded, &err)) {
        return "Load failed: " + err;
    }
    const struct {
        const char* id;
        SourceKind kind;
        const char* param;
        const char* name;
        int track;
        double start;
        double duration;
    } kExpect[] = {
        {"matte", SourceKind::ColorMatte, "#3366CC", "Backdrop",
         0, 0.0, 6.0},
        {"bars", SourceKind::ColorBars, "smpte75", "", 1, 0.0, 3.0},
        {"title", SourceKind::Text, "Smoke | size=24", "Smoke Title",
         1, 3.0, 3.0},
    };
    for (const auto& expect : kExpect) {
        const Clip* clip = FindClip(loaded, expect.id);
        if (clip == nullptr || clip->source.kind != expect.kind ||
            clip->source.param != expect.param ||
            clip->name != expect.name || clip->track != expect.track ||
            clip->timeline_start != expect.start ||
            clip->duration != expect.duration) {
            return "generated clip did not survive the round trip: " +
                   std::string(expect.id);
        }
    }
    return {};
}

// Shared import filter (smoke_checks.h) — the same predicate the
// file dialog's pattern list and the OS file-drop filter are built
// from: common video/audio/still extensions accept (any case),
// obvious non-media and edge cases (no dot, trailing dot, dot inside
// a directory name, near-miss extensions) reject.
std::string CheckMediaImportFilter() {
    for (const char* path :
         {"a.mp4", "B.MP4", "x/song.flac", "pic.PNG", "clip.m2ts",
          "still.dng", "C:\\dir\\clip.mkv"}) {
        if (!IsMediaImportPath(path)) {
            return std::string("rejected media path: ") + path;
        }
    }
    for (const char* path :
         {"a.txt", "b.xerproj", "c.exe", "noext", "trailing.",
          "dir.name/file", "archive.mp4x", "xmp4"}) {
        if (IsMediaImportPath(path)) {
            return std::string("accepted non-media path: ") + path;
        }
    }
    return {};
}

// LUT .cube parser (lut_manager.h) — synthetic strings only; no file
// system or OCIO needed. TITLE/size/data extraction on a well-formed
// 3D cube, tolerant-but-warned short bodies, hard reject on cubes with
// no declared size or no data rows, and the 1D variant.
std::string CheckLutManager() {
    const std::string cube =
        "# leading comment\n"
        "TITLE \"Smoky Two\"\n"
        "LUT_3D_SIZE 2\n"
        "DOMAIN_MIN 0 0 0\n"
        "DOMAIN_MAX 1 1 1\n"
        "0 0 0\n1 0 0\n0 1 0\n0 0 1\n"
        "1 1 0\n1 0 1\n0 1 1\n1 1 1 # trailing comment\n";
    Lut::CubeData data;
    std::string error;
    if (!Lut::ParseCubeText(cube, &data, &error)) {
        return "ParseCubeText rejected a valid cube: " + error;
    }
    if (data.title != "Smoky Two" || !data.is_3d || data.size != 2 ||
        data.data.size() != 24) {
        return "ParseCubeText produced wrong title/size/data";
    }
    // Short body: parse succeeds but surfaces the row-count warning.
    error.clear();
    if (!Lut::ParseCubeText("LUT_3D_SIZE 2\n0 0 0\n", &data, &error) ||
        error.empty()) {
        return "short cube body missed its warning";
    }
    // No declared size / no data rows are hard failures.
    if (Lut::ParseCubeText("TITLE \"x\"\n0 0 0\n", &data, nullptr)) {
        return "size-less cube accepted";
    }
    if (Lut::ParseCubeText("LUT_3D_SIZE 2\n", &data, nullptr)) {
        return "empty-body cube accepted";
    }
    // 1D variant: size rows, not size^3.
    error.clear();
    if (!Lut::ParseCubeText("LUT_1D_SIZE 2\n0 0 0\n1 1 1\n", &data,
                            &error) ||
        data.is_3d || data.size != 2 || data.data.size() != 6) {
        return "1D cube parse failed";
    }
    return {};
}

// URL import helpers (url_import.h) — table-driven classification
// (platform host / direct / invalid) plus the filename machinery:
// Content-Disposition extraction, URL basename, percent-decoding and
// Windows filename sanitizing. No network — Download() itself is
// exercised live, never in smoke.
std::string CheckUrlImport() {
    using UrlKind = UrlImport::UrlKind;
    const struct {
        const char* url;
        UrlKind kind;
    } cases[] = {
        {"https://www.youtube.com/watch?v=abc", UrlKind::kPlatform},
        {"https://youtu.be/abc", UrlKind::kPlatform},
        {"https://m.youtube.com/watch?v=x", UrlKind::kPlatform},
        {"https://vimeo.com/12345", UrlKind::kPlatform},
        {"https://www.tiktok.com/@u/video/1", UrlKind::kPlatform},
        {"https://www.instagram.com/reel/x/", UrlKind::kPlatform},
        {"https://clips.twitch.tv/x", UrlKind::kPlatform},
        {"HTTPS://YOUTUBE.COM/watch?v=x", UrlKind::kPlatform},
        // Suffix lookalikes must NOT classify as the platform.
        {"https://notyoutube.com/x", UrlKind::kDirect},
        {"https://youtube.com.evil.com/x", UrlKind::kDirect},
        {"https://cdn.example.com/clip.mp4", UrlKind::kDirect},
        {"http://files.host/a.wav?dl=1", UrlKind::kDirect},
        {"https://user@h.example.com/m.mkv", UrlKind::kDirect},
        {"ftp://h/f.mp4", UrlKind::kInvalid},
        {"file:///c:/a.mp4", UrlKind::kInvalid},
        {"C:/media/a.mp4", UrlKind::kInvalid},
        {"youtube.com/watch?v=x", UrlKind::kInvalid},
        {"https:///x", UrlKind::kInvalid},
        {"", UrlKind::kInvalid},
    };
    for (const auto& c : cases) {
        if (UrlImport::ClassifyUrl(c.url) != c.kind) {
            return std::string("ClassifyUrl(\"") + c.url +
                   "\") wrong kind";
        }
    }
    // Host extraction (used for status text + platform match).
    if (UrlImport::HostOf("https://YouTube.COM:443/watch") !=
        "youtube.com") {
        return "HostOf failed port/case handling";
    }
    // Filename machinery.
    if (UrlImport::FilenameFromContentDisposition(
            "attachment; filename=\"clip final.mp4\"") !=
        "clip final.mp4") {
        return "Content-Disposition quoted filename failed";
    }
    if (UrlImport::FilenameFromContentDisposition(
            "attachment; filename*=UTF-8''%E2%82%AC%20clip.mp4") !=
        "\xE2\x82\xAC clip.mp4") {
        return "Content-Disposition RFC5987 filename failed";
    }
    if (!UrlImport::FilenameFromContentDisposition("inline").empty()) {
        return "Content-Disposition phantom filename";
    }
    if (UrlImport::UrlBasename(
            "https://h/a/b/my%20clip.mp4?dl=1#f") != "my clip.mp4" ||
        !UrlImport::UrlBasename("https://h/a/").empty() ||
        !UrlImport::UrlBasename("https://h").empty()) {
        return "UrlBasename failed";
    }
    if (UrlImport::PercentDecode("%41%42") != "AB") {
        return "PercentDecode failed";
    }
    struct {
        const char* in;
        const char* want;
    } sanitize[] = {
        {"a<b>c:d\"e/f\\g|h?i*j", "a_b_c_d_e_f_g_h_i_j"},
        {"clip.mp4", "clip.mp4"},
        {"name.", "name"},
        {"...", "download"},
        {"", "download"},
    };
    for (const auto& c : sanitize) {
        if (UrlImport::SanitizeFilename(c.in) != c.want) {
            return std::string("SanitizeFilename(\"") + c.in +
                   "\") wrong";
        }
    }
    return {};
}

// Extensions panel data path: when assets/extensions/ holds .xexrj
// packages, ExtHost::Shared().Scan() must surface them to the Ext
// dock tab's list (id/name/version present) and a UI-declaring
// extension must resolve a usable xer-ext:// URL + member bytes —
// exactly what the panel's "Open" action consumes. Missing assets
// are tolerated (fresh builds may deploy none).
std::string CheckExtensionsPanelData() {
    std::string status;
    auto boot = AppRuntime::Boot(&status);  // resolves assets_dir
    AppRuntime::SharedBootEngine() = nullptr;
    const auto& cfg = AppRuntime::SharedAssetsConfig();
    if (cfg.assets_dir.empty()) {
        return {};   // no assets deployed — nothing to list
    }
    const std::filesystem::path ext_dir =
        cfg.assets_dir / "extensions";
    std::error_code ec;
    bool any_package = false;
    if (std::filesystem::is_directory(ext_dir, ec)) {
        for (const auto& de :
             std::filesystem::directory_iterator(ext_dir, ec)) {
            if (de.path().extension() == ".xexrj") {
                any_package = true;
                break;
            }
        }
    }
    if (!any_package) {
        return {};   // tolerant: extensions are optional content
    }
    ExtHost::Shared().Scan(ext_dir.string());
    const auto& entries = ExtHost::Shared().Entries();
    if (entries.empty()) {
        return "extensions present but scan listed none";
    }
    int verified = 0;
    const Extension* with_ui = nullptr;
    for (const Extension& e : entries) {
        if (!e.verified) continue;
        ++verified;
        if (e.manifest.id.empty() || e.manifest.name.empty()) {
            return "verified extension missing id/name in manifest";
        }
        if (!e.manifest.ui.empty() && with_ui == nullptr) {
            with_ui = &e;
        }
    }
    if (verified == 0) {
        return "extensions present but none verified";
    }
    if (with_ui != nullptr) {
        // The panel builds this URL for its Open action.
        const std::string url =
            XER::Cef::ExtSchemeUrl(with_ui->manifest.id,
                                   with_ui->manifest.ui);
        const std::string want_prefix =
            "xer-ext://" + with_ui->manifest.id + "/";
        if (url.rfind(want_prefix, 0) != 0) {
            return "ExtSchemeUrl produced '" + url + "'";
        }
        if (ExtHost::Shared()
                .ReadMember(with_ui->manifest.id, with_ui->manifest.ui)
                .empty()) {
            return "declared UI member unreadable: " +
                   with_ui->manifest.ui;
        }
    }
    return {};
}

// .xexrj native JIT payloads (tools/tz/xsjo_compile.*): ztzmake packs
// extension C++ sources as COFF objects under mobjective/*.xsjo, each
// wrapped in an XSJ1 header (magic, version, triple, source name).
// The JIT loader trusts that header before executing the object, so
// this check re-validates it on every deployed extension: well-formed
// header + a COFF machine word matching the triple. No .xexrj or no
// .xsjo member degrades gracefully (assets_icons owns missing-package
// complaints, and non-Clang toolchains legitimately ship none); a
// malformed .xsjo is a hard failure — the pipeline produced a broken
// object.
std::string CheckExtXsjo() {
    std::string status;
    AppRuntime::Boot(&status);   // resolves assets_dir
    AppRuntime::SharedBootEngine() = nullptr;
    const auto& cfg = AppRuntime::SharedAssetsConfig();
    if (cfg.assets_dir.empty()) {
        return {};
    }
    const std::filesystem::path ext_dir = cfg.assets_dir / "extensions";
    std::error_code ec;
    if (!std::filesystem::is_directory(ext_dir, ec)) {
        return {};
    }
    const auto u16le = [](const std::uint8_t* p) {
        return static_cast<unsigned>(p[0]) |
               (static_cast<unsigned>(p[1]) << 8);
    };
    const auto u64le = [](const std::uint8_t* p) {
        std::uint64_t x = 0;
        for (int s = 0; s < 64; s += 8) {
            x |= static_cast<std::uint64_t>(p[s / 8]) << s;
        }
        return x;
    };
    for (const auto& de :
         std::filesystem::directory_iterator(ext_dir, ec)) {
        if (de.path().extension() != ".xexrj") continue;
        std::string open_err;
        auto arc =
            Tz::TzArchive::Open(Utf8Path(de.path()), &open_err);
        if (!arc) {
            return "cannot open " +
                   Utf8Path(de.path().filename()) + ": " + open_err;
        }
        for (const auto& name : arc->Names()) {
            if (name.rfind("mobjective/", 0) != 0 ||
                name.size() < 6 ||
                name.compare(name.size() - 5, 5, ".xsjo") != 0) {
                continue;
            }
            std::vector<std::uint8_t> bytes;
            if (!arc->Read(name, &bytes)) {
                return "cannot read member " + name;
            }
            const std::uint8_t* p = bytes.data();
            const std::size_t n = bytes.size();
            const auto bad = [&](const char* why) {
                return name + ": " + why;
            };
            if (n < 14 || p[0] != 'X' || p[1] != 'S' || p[2] != 'J' ||
                p[3] != '1') {
                return bad("bad XSJ1 magic");
            }
            if (u16le(p + 4) != 1) {
                return bad("unsupported XSJ1 version");
            }
            std::size_t at = 6;
            std::string triple;
            for (int i = 0; i < 2; ++i) {   // triple, source name
                if (at + 2 > n) return bad("truncated header");
                const unsigned len = u16le(p + at);
                at += 2;
                if (at + len > n) return bad("truncated strings");
                if (i == 0) {
                    triple.assign(
                        reinterpret_cast<const char*>(p + at), len);
                }
                at += len;
            }
            if (triple.empty()) return bad("empty triple");
            if (at + 8 > n) return bad("missing payload size");
            const std::uint64_t payload_size = u64le(p + at);
            at += 8;
            if (payload_size < 20 || payload_size > n - at) {
                return bad("payload size out of range");
            }
            // COFF object file header starts with the machine type:
            // x86_64 triples must carry IMAGE_FILE_MACHINE_AMD64.
            if (triple.rfind("x86_64", 0) == 0 &&
                u16le(p + at) != 0x8664) {
                return bad("payload is not an x86_64 COFF object");
            }
        }
    }
    return {};
}

// VE.ExtensionsRuntime.exe (extensions/ext_runtime_server.cc) — the
// dedicated native-extension JIT host: the exe must sit beside
// video_editor.exe (build/deploy contract), ping must report the
// VE.ExtensionsRuntime role, ext_list must surface the deployed
// demo.xexrj, and ext_load on the JS-only demo must succeed
// gracefully (native:false — no mobjective members → the in-editor
// ExtHost owns it). Any listed extension that DOES carry
// mobjective/*.xsjo is also ext_loaded — tolerated when the JIT is
// unavailable or the reply is a clean ok:false engine error (the
// ztzmake-side native demo may not be packed yet); transport silence
// and malformed replies still count as failures.
std::string CheckExtRuntime() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(
            bin_dir / "VE.ExtensionsRuntime.exe", ec)) {
        return "VE.ExtensionsRuntime.exe missing beside "
               "video_editor.exe";
    }
    WorkerPipe pipe(WorkerPipeSpec{
        "VE.ExtensionsRuntime.exe", nullptr, "XER_VE_EXT_WORKER"});
    if (!pipe.Available()) {
        return "ext runtime spawn failed";
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        return "ext runtime ping failed";
    }
    if (role != "VE.ExtensionsRuntime") {
        return "unexpected ext runtime role '" + role + "'";
    }

    const auto round_trip =
        [&](const std::function<void(Json::Value&)>& fill,
            std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            Json::Value req(Json::objectValue);
            req["id"] = Json::UInt64(id);
            fill(req);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return pipe.RoundTrip(id, Json::writeString(w, req), 15000,
                                  reply);
        };
    const auto parse = [](const std::string& line, Json::Value* out) {
        std::string errs;
        Json::CharReaderBuilder r;
        std::istringstream in(line);
        return Json::parseFromStream(r, in, out, &errs) &&
               out->isObject();
    };

    // ext_list — must find the deployed demo.xexrj.
    const std::string ext_dir =
        Utf8Path(bin_dir / "assets" / "extensions");
    std::string reply;
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_list";
                r["dir"] = ext_dir;
            },
            &reply)) {
        return "ext_list got no reply";
    }
    Json::Value list;
    if (!parse(reply, &list) || !list.get("ok", false).asBool()) {
        return "ext_list failed: " +
               list.get("err", "malformed reply").asString();
    }
    std::string demo_path;
    std::vector<std::string> native_paths;
    for (const auto& e : list["exts"]) {
        if (e.get("id", "").asString() == "demo") {
            demo_path = e.get("path", "").asString();
        }
        if (e.get("has_native", false).asBool()) {
            native_paths.push_back(e.get("path", "").asString());
        }
    }
    if (demo_path.empty()) {
        return "demo.xexrj not reported by ext_list";
    }

    // ext_load demo — ok:true is required. native:true means the
    // demo's mobjective/*.xsjo went through the real JIT path
    // (XSJ1 parse → COFF → LLJIT link → module loaded); native:false
    // is the graceful JS-only shape.
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_load";
                r["path"] = demo_path;
            },
            &reply)) {
        return "ext_load demo got no reply";
    }
    Json::Value load;
    if (!parse(reply, &load)) {
        return "ext_load demo reply is malformed";
    }
    if (!load.get("ok", false).asBool()) {
        return "ext_load demo failed: " +
               load.get("err", "?").asString();
    }
    if (!load.isMember("native")) {
        return "ext_load demo reply lacks the native flag";
    }

    // When the demo packed native objects, drive one exported
    // callable end to end — ext_call "ve_demo_name" exercises the
    // JIT symbol lookup, the JIT'd call itself and the ve_ctx
    // pass-through. ok:true with a non-empty result_json is
    // asserted; ok:false is tolerated (the demo's exports may move).
    if (load.get("native", false).asBool()) {
        reply.clear();
        if (!round_trip(
                [&](Json::Value& r) {
                    r["kind"] = "ext_call";
                    r["ext_id"] = "demo";
                    r["fn"] = "ve_demo_name";
                    r["args"] = "{}";
                },
                &reply)) {
            return "ext_call ve_demo_name got no reply";
        }
        Json::Value call;
        if (!parse(reply, &call)) {
            return "ext_call ve_demo_name reply is malformed";
        }
        if (call.get("ok", false).asBool() &&
            call.get("result_json", "").asString().empty()) {
            return "ext_call ve_demo_name returned an empty result";
        }
        // ok:false tolerated — the demo's exported callables may
        // change; the JIT path already ran in ext_load.
        reply.clear();
        if (!round_trip(
                [&](Json::Value& r) {
                    r["kind"] = "ext_unload";
                    r["ext_id"] = "demo";
                },
                &reply)) {
            return "ext_unload got no reply";
        }
    }

    // ext_events — drains the queued-event bridge; empty is fine.
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) { r["kind"] = "ext_events"; },
            &reply)) {
        return "ext_events got no reply";
    }
    Json::Value ev;
    if (!parse(reply, &ev) || !ev.get("ok", false).asBool() ||
        !ev["events"].isArray()) {
        return "ext_events reply malformed";
    }

    // Tolerated JIT drive — see the comment above the function.
    for (const auto& p : native_paths) {
        reply.clear();
        if (!round_trip(
                [&](Json::Value& r) {
                    r["kind"] = "ext_load";
                    r["path"] = p;
                },
                &reply)) {
            return "ext_load of a native extension got no reply";
        }
        if (!parse(reply, &load)) {
            return "ext_load native reply malformed";
        }
        // ok:false tolerated until the native demo lands.
    }
    pipe.KillWorker();
    return {};
#else
    return {};
#endif
}

// ext_bridge — the v2 host channel (sdk/ve.core.h, worker_pipe's
// callback_env reverse channel). Drives the REAL ExtRuntimePipe
// singleton so the spawned VE.ExtensionsRuntime gets the
// XER_VE_EXT_HOST_* handle handoff the editor would give it, then:
//   1. ext_call "ve_demo_add_clip" — the fire-and-forget path; the
//      reply's events[] must carry a well-formed "add_clip" event.
//   2. ext_call "ve_demo_hostcall" on a SECOND thread while this one
//      services the reverse channel — the extension's ve_call parks
//      inside the runtime until our host_call reply unblocks it; the
//      callable echoes the editor's result back through result_json.
// Tolerated: missing exe/JIT, a demo packed before these exports
// existed (ok:false symbol errors), or a worker spawned without the
// channel. Hard failures: transport silence and malformed shapes.
std::string CheckExtBridge() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(
            bin_dir / "VE.ExtensionsRuntime.exe", ec)) {
        return {};   // ext_runtime owns the missing-exe complaint
    }
    WorkerPipe& pipe = ExtRuntimePipe();
    if (!pipe.Available()) {
        return {};   // tolerated — spawn may be disabled
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        return "ext runtime ping failed";
    }
    if (role != "VE.ExtensionsRuntime") {
        return "unexpected ext runtime role '" + role + "'";
    }

    const auto round_trip =
        [&](const std::function<void(Json::Value&)>& fill,
            int timeout_ms, std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            Json::Value req(Json::objectValue);
            req["id"] = Json::UInt64(id);
            fill(req);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return pipe.RoundTrip(id, Json::writeString(w, req),
                                  timeout_ms, reply);
        };
    const auto parse = [](const std::string& line, Json::Value* out) {
        std::string errs;
        Json::CharReaderBuilder r;
        std::istringstream in(line);
        return Json::parseFromStream(r, in, out, &errs) &&
               out->isObject();
    };

    // Find + load the demo. native:false/ok:false both skip the rest
    // gracefully (JS-only pack, or no LLVM in this toolchain).
    const std::string ext_dir =
        Utf8Path(bin_dir / "assets" / "extensions");
    std::string reply;
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_list";
                r["dir"] = ext_dir;
            },
            15000, &reply)) {
        return "ext_list got no reply";
    }
    Json::Value list;
    if (!parse(reply, &list) || !list.get("ok", false).asBool()) {
        return "ext_list reply malformed";
    }
    std::string demo_path;
    for (const auto& e : list["exts"]) {
        if (e.get("id", "").asString() == "demo") {
            demo_path = e.get("path", "").asString();
        }
    }
    if (demo_path.empty()) {
        return {};   // no demo deployed — nothing to bridge
    }
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_load";
                r["path"] = demo_path;
            },
            60000, &reply)) {
        return "ext_load demo got no reply";
    }
    Json::Value load;
    if (!parse(reply, &load) || !load.get("ok", false).asBool() ||
        !load.get("native", false).asBool()) {
        return {};   // JIT unavailable or JS-only pack — tolerated
    }

    // 1. Queued add_clip event — must arrive in the ext_call reply's
    //    events[] with the exact shape the editor pump consumes.
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_call";
                r["ext_id"] = "demo";
                r["fn"] = "ve_demo_add_clip";
                r["args"] = "{}";
            },
            30000, &reply)) {
        return "ext_call ve_demo_add_clip got no reply";
    }
    Json::Value call;
    if (!parse(reply, &call)) {
        return "ext_call ve_demo_add_clip reply is malformed";
    }
    if (call.get("ok", false).asBool()) {
        const Json::Value& events = call["events"];
        if (!events.isArray()) {
            return "ext_call reply carries no events array";
        }
        const Json::Value* add = nullptr;
        for (const auto& e : events) {
            if (e.get("type", "").asString() == "add_clip") {
                add = &e;
            }
        }
        if (add == nullptr) {
            return "ve_demo_add_clip produced no add_clip event";
        }
        if (add->get("kind", "").asString() != "color" ||
            std::fabs(add->get("start", 0.0).asDouble() - 1.0) >
                1e-6 ||
            std::fabs(add->get("duration", 0.0).asDouble() - 2.5) >
                1e-6) {
            return "add_clip event fields are wrong";
        }
    }
    // ok:false tolerated — a demo packed before the export existed.

    // 2. Synchronous host channel — ve_call parked in the runtime is
    //    answered by OUR callback reply, proving the reverse channel
    //    end to end without the editor UI.
    if (!pipe.HasCallbackChannel()) {
        return {};   // worker spawned without the channel — tolerated
    }
    {
        std::atomic<bool> done{false};
        std::string call_reply;
        std::thread caller([&] {
            std::string r;
            static_cast<void>(round_trip(
                [&](Json::Value& req) {
                    req["kind"] = "ext_call";
                    req["ext_id"] = "demo";
                    req["fn"] = "ve_demo_hostcall";
                    req["args"] = "{}";
                },
                120000, &r));
            call_reply = std::move(r);
            done.store(true, std::memory_order_release);
        });
        bool saw_host_call = false;
        std::string failure;   // deferred — caller must join first
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(10);
        while (!done.load(std::memory_order_acquire) &&
               failure.empty() &&
               std::chrono::steady_clock::now() < deadline) {
            std::string line;
            if (!pipe.PopCallbackLine(&line)) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(5));
                continue;
            }
            Json::Value req;
            if (!parse(line, &req) ||
                req.get("kind", "").asString() != "host_call") {
                continue;
            }
            saw_host_call = true;
            if (req.get("name", "").asString() != "project.info") {
                failure = "host_call asked for an unexpected service";
                break;
            }
            Json::Value rsp(Json::objectValue);
            rsp["req_id"] = req["req_id"];
            rsp["ok"] = true;
            rsp["result"] = "{\"ok\":true,\"via\":\"smoke\"}";
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            if (!pipe.WriteCallbackLine(
                    Json::writeString(w, rsp))) {
                failure = "host_call reply write failed";
            }
        }
        if (!done.load(std::memory_order_acquire)) {
            // Unstick the parked RoundTrip before joining — a kill
            // breaks every pipe and fails the wait fast.
            pipe.KillWorker();
        }
        caller.join();
        if (!failure.empty()) {
            return failure;
        }
        if (!done.load(std::memory_order_acquire)) {
            return saw_host_call
                       ? "ve_call never returned after the host "
                         "reply — bridge broken"
                       : "ext_call hung with no host_call — demo "
                         "stale?";
        }
        if (call_reply.empty()) {
            return "ext_call ve_demo_hostcall got no reply";
        }
        Json::Value host;
        if (!parse(call_reply, &host)) {
            return "ve_demo_hostcall reply is malformed";
        }
        if (!host.get("ok", false).asBool()) {
            return {};   // symbol absent in this pack — tolerated
        }
        // The demo echoes ve_call's raw result: our editor reply must
        // be visible inside result_json.
        const std::string result =
            host.get("result_json", "").asString();
        if (result.find("\"via\":\"smoke\"") == std::string::npos) {
            return "ve_call did not return the editor's reply: " +
                   result;
        }
    }

    // Best-effort unload.
    reply.clear();
    static_cast<void>(round_trip(
        [&](Json::Value& r) {
            r["kind"] = "ext_unload";
            r["ext_id"] = "demo";
        },
        15000, &reply));
    return {};
#else
    return {};
#endif
}

// ext_xejslib — the .xejslib library chain end to end (tools/tz/
// xejslib.h, XSJOBuildX): when demo.xexrj carries
// mobjective/lib_*.xsjo members — spliced in by ztzmake --dep from
// libdemo.xejslib — ext_load must resolve the extension's own
// undefined libdemo:: symbols against them (the runtime meshes all
// of one extension's JITDylibs). Driving ve_demo_gain proves a
// mangled C++ symbol crosses module boundaries: result_json must
// carry libdemo's Version() string from the LIB module's rodata.
// Tolerated: no demo, no lib_* members (packed without the dep),
// missing runtime/JIT, or a stale pack where ve_demo_gain does not
// resolve. A present-but-wrong result is a hard failure.
std::string CheckExtXejslib() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    const std::filesystem::path ext_dir =
        bin_dir / "assets" / "extensions";
    const std::filesystem::path demo = ext_dir / "demo.xexrj";
    if (!std::filesystem::is_regular_file(demo, ec)) {
        return {};   // assets_icons owns the missing-package report
    }
    std::string open_err;
    auto arc = Tz::TzArchive::Open(Utf8Path(demo), &open_err);
    if (!arc) {
        return "cannot open demo.xexrj: " + open_err;
    }
    bool has_lib_member = false;
    for (const auto& name : arc->Names()) {
        if (name.rfind("mobjective/lib_", 0) == 0 &&
            name.size() > 5 &&
            name.compare(name.size() - 5, 5, ".xsjo") == 0) {
            has_lib_member = true;
            break;
        }
    }
    if (!has_lib_member) {
        return {};   // packed without a dep — nothing to prove
    }
    if (!std::filesystem::exists(
            bin_dir / "VE.ExtensionsRuntime.exe", ec)) {
        return {};   // ext_runtime owns the missing-exe complaint
    }
    WorkerPipe pipe(WorkerPipeSpec{
        "VE.ExtensionsRuntime.exe", nullptr, "XER_VE_EXT_WORKER"});
    if (!pipe.Available()) {
        return "ext runtime spawn failed";
    }
    const auto round_trip =
        [&](const std::function<void(Json::Value&)>& fill,
            std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            Json::Value req(Json::objectValue);
            req["id"] = Json::UInt64(id);
            fill(req);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return pipe.RoundTrip(id, Json::writeString(w, req),
                                  30000, reply);
        };
    const auto parse = [](const std::string& line, Json::Value* out) {
        std::string errs;
        Json::CharReaderBuilder r;
        std::istringstream in(line);
        return Json::parseFromStream(r, in, out, &errs) &&
               out->isObject();
    };
    std::string reply;
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_load";
                r["path"] = Utf8Path(demo);
            },
            &reply)) {
        return "ext_load demo got no reply";
    }
    Json::Value load;
    if (!parse(reply, &load)) {
        return "ext_load demo reply is malformed";
    }
    if (!load.get("ok", false).asBool() ||
        !load.get("native", false).asBool()) {
        return {};   // JIT unavailable — ext_runtime reports it
    }
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_call";
                r["ext_id"] = "demo";
                r["fn"] = "ve_demo_gain";
                r["args"] = "{}";
            },
            &reply)) {
        return "ext_call ve_demo_gain got no reply";
    }
    // Best-effort unload before evaluating the reply.
    std::string unreply;
    static_cast<void>(round_trip(
        [&](Json::Value& r) {
            r["kind"] = "ext_unload";
            r["ext_id"] = "demo";
        },
        &unreply));
    Json::Value call;
    if (!parse(reply, &call)) {
        return "ext_call ve_demo_gain reply is malformed";
    }
    if (!call.get("ok", false).asBool()) {
        return {};   // symbol absent in this pack — tolerated
    }
    const std::string result =
        call.get("result_json", "").asString();
    if (result.find("libdemo/1.0") == std::string::npos) {
        return "ve_demo_gain did not report libdemo's version "
               "(cross-module link failed?): " + result;
    }
    if (result.find("\"gain_q15\"") == std::string::npos ||
        result.find("\"out\"") == std::string::npos) {
        return "ve_demo_gain result lacks computed gain fields: " +
               result;
    }
    pipe.KillWorker();
    return {};
#else
    return {};
#endif
}

// ve_core_v3 — the ve_call service-table contract (sdk/ve.core.h):
//   1. VeServiceNames() coverage: every advertised namespace head is
//      present, VeServiceKnown() agrees with the list, and the
//      deliberate exclusions (app.quit, v2 aliases, garbage) stay
//      unadvertised.
//   2. The describe layer (project_store.cc) answers real project
//      JSON for a fabricated project — the same builders
//      DispatchExtHostCall serves editor-side, so the served shapes
//      are asserted here without a window.
//   3. Every Transition::Type round-trips
//      TransitionTypeToString/FromString — .xerproj files and the
//      "timeline.add_transition" arg vocabulary share that table.
//   4. Protocol end to end: the demo extension's ve_call reaches a
//      mini dispatcher over the REAL reverse channel (the same pump
//      the editor's UI tick runs). "project.info" answers with the
//      fabricated describe and "ve.services" with the real table —
//      the extension's result_json must carry both back verbatim.
// Tolerated on the pipe path: missing runtime/JIT, a stale demo
// pack, no channel (ext_bridge owns the transport complaints).
// The static parts always run.
std::string CheckVeCoreV3() {
    // 1. Service-table coverage + self-consistency.
    const auto& names = VeServiceNames();
    if (names.size() < 40) {
        return "VeServiceNames is suspiciously short (" +
               std::to_string(names.size()) + " entries)";
    }
    for (const char* required : {
             "ve.services", "app.info",
             "project.info", "project.new", "project.open",
             "project.save", "project.markers",
             "timeline.info", "timeline.tracks", "timeline.clips",
             "timeline.add_track", "timeline.remove_track",
             "timeline.rename_track", "timeline.set_track",
             "timeline.add_clip", "timeline.move_clip",
             "timeline.remove_clip", "timeline.trim_clip",
             "timeline.split_clip", "timeline.add_transition",
             "timeline.remove_transition",
             "transport.info", "transport.play", "transport.pause",
             "transport.stop", "transport.seek",
             "transport.frame_step", "transport.set_loop",
             "transport.set_rate",
             "selection.get", "selection.set",
             "media.list", "media.import", "media.refresh",
             "fx.list", "fx.add", "fx.remove",
             "export.start", "export.cancel", "export.status",
             "ui.status", "ui.open_panel",
             "edit.undo", "edit.redo",
         }) {
        if (!VeServiceKnown(required)) {
            return std::string("service missing from VeServiceNames: ") +
                   required;
        }
    }
    for (const std::string& n : names) {
        if (!VeServiceKnown(n)) {
            return "VeServiceNames/VeServiceKnown disagree on " + n;
        }
        if (n != "ve.services" && n != "app.info" &&
            n.find('.') == std::string::npos) {
            return "unnamespaced service name " + n;
        }
    }
    // The deliberate exclusions: quit never ships, and the v2
    // aliases are still served but never advertised.
    if (VeServiceKnown("app.quit") || VeServiceKnown("project.clips") ||
        VeServiceKnown("status") || VeServiceKnown("bogus.service")) {
        return "VeServiceKnown advertised an unserved name";
    }

    // 2. Describe layer answers real project state.
    {
        Project project = MakeRenderProject(160, 90, 15);
        Marker marker;
        marker.time = 0.25;
        marker.name = "smoke-mark";
        project.markers.push_back(marker);
        const Json::Value info =
            VeDescribeProject(project, "demo.xerproj", true);
        if (info.get("width", 0).asInt() != 160 ||
            info.get("height", 0).asInt() != 90 ||
            info.get("fps_num", 0).asInt() != 15 ||
            info.get("markers", Json::UInt64(0)).asUInt64() != 1 ||
            !info.get("dirty", false).asBool()) {
            return "VeDescribeProject fields are wrong";
        }
        const Json::Value clips = VeDescribeClips(project);
        if (!clips["clips"].isArray() || clips["clips"].size() != 1 ||
            clips["clips"][0].get("kind", "").asString() != "bars" ||
            clips["clips"][0].get("id", "").asString() != "bars") {
            return "VeDescribeClips fields are wrong";
        }
        const Json::Value tracks = VeDescribeTracks(project);
        if (!tracks["tracks"].isArray() ||
            tracks["tracks"].size() != 2 ||
            tracks["tracks"][0].get("kind", "").asString() != "video" ||
            tracks["tracks"][1].get("kind", "").asString() != "audio") {
            return "VeDescribeTracks fields are wrong";
        }
        const Json::Value timeline =
            VeDescribeTimeline(project, 0.5, "bars");
        if (std::fabs(timeline.get("playhead", -1.0).asDouble() - 0.5) >
                1e-9 ||
            timeline.get("selected_clip", "").asString() != "bars" ||
            std::fabs(timeline.get("frame_seconds", -1.0).asDouble() -
                      1.0 / 15.0) > 1e-9) {
            return "VeDescribeTimeline fields are wrong";
        }
        const Json::Value markers = VeDescribeMarkers(project);
        if (!markers["markers"].isArray() ||
            markers["markers"].size() != 1 ||
            std::fabs(markers["markers"][0].get("time", -1.0)
                          .asDouble() - 0.25) > 1e-9) {
            return "VeDescribeMarkers fields are wrong";
        }
    }

    // 3. Transition wire names round-trip for every enum value, and
    //    garbage is rejected (add_transition's bad_type error).
    for (Transition::Type t : {Transition::Type::Crossfade,
                               Transition::Type::WipeLeft,
                               Transition::Type::SlideDown,
                               Transition::Type::DipToBlack,
                               Transition::Type::AdditiveDissolve,
                               Transition::Type::Script}) {
        Transition::Type back = Transition::Type::Crossfade;
        const char* text = TransitionTypeToString(t);
        if (text == nullptr || *text == '\0' ||
            !TransitionTypeFromString(text, &back) || back != t) {
            return "transition wire-name round trip failed";
        }
    }
    {
        Transition::Type t;
        if (TransitionTypeFromString("not_a_type", &t)) {
            return "unknown transition name parsed";
        }
    }

#if defined(_WIN32)
    // 4. Protocol round trip + one REAL describe call through the
    //    reverse channel (the demo's ve_demo_hostcall /
    //    ve_demo_services exports drive the extension side).
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path bin_dir =
        std::filesystem::path(exe).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(
            bin_dir / "VE.ExtensionsRuntime.exe", ec)) {
        return {};   // ext_runtime owns the missing-exe complaint
    }
    WorkerPipe& pipe = ExtRuntimePipe();
    if (!pipe.Available()) {
        return {};   // tolerated — spawn may be disabled
    }
    if (!pipe.HasCallbackChannel()) {
        return {};   // worker spawned without the channel — tolerated
    }
    const auto round_trip =
        [&](const std::function<void(Json::Value&)>& fill,
            int timeout_ms, std::string* reply) {
            const std::uint64_t id = pipe.NextId();
            Json::Value req(Json::objectValue);
            req["id"] = Json::UInt64(id);
            fill(req);
            Json::StreamWriterBuilder w;
            w["indentation"] = "";
            return pipe.RoundTrip(id, Json::writeString(w, req),
                                  timeout_ms, reply);
        };
    const auto parse = [](const std::string& line, Json::Value* out) {
        std::string errs;
        Json::CharReaderBuilder r;
        std::istringstream in(line);
        return Json::parseFromStream(r, in, out, &errs) &&
               out->isObject();
    };
    const std::string ext_dir =
        Utf8Path(bin_dir / "assets" / "extensions");
    std::string reply;
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_list";
                r["dir"] = ext_dir;
            },
            15000, &reply)) {
        return "ext_list got no reply";
    }
    Json::Value list;
    if (!parse(reply, &list) || !list.get("ok", false).asBool()) {
        return "ext_list reply malformed";
    }
    std::string demo_path;
    for (const auto& e : list["exts"]) {
        if (e.get("id", "").asString() == "demo") {
            demo_path = e.get("path", "").asString();
        }
    }
    if (demo_path.empty()) {
        return {};   // no demo deployed — nothing to drive
    }
    reply.clear();
    if (!round_trip(
            [&](Json::Value& r) {
                r["kind"] = "ext_load";
                r["path"] = demo_path;
            },
            60000, &reply)) {
        return "ext_load demo got no reply";
    }
    Json::Value load;
    if (!parse(reply, &load) || !load.get("ok", false).asBool() ||
        !load.get("native", false).asBool()) {
        return {};   // JIT unavailable or JS-only pack — tolerated
    }

    // The mini dispatcher answers host_call lines the way
    // DispatchExtHostCall does: "project.info" serves the fabricated
    // project's describe, "ve.services" the real table, anything
    // else the contract's {"ok":false,"error":"unknown_call"}.
    const Project demo_project = MakeRenderProject(160, 90, 15);
    const auto compact = [](const Json::Value& v) {
        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        return Json::writeString(w, v);
    };
    const auto answer = [&](const Json::Value& req) -> std::string {
        Json::Value rsp(Json::objectValue);
        rsp["req_id"] = req["req_id"];
        rsp["ok"] = true;
        const std::string svc = req.get("name", "").asString();
        if (svc == "project.info") {
            Json::Value body =
                VeDescribeProject(demo_project, "", false);
            body["ok"] = true;
            rsp["result"] = compact(body);
        } else if (svc == "ve.services") {
            Json::Value body(Json::objectValue);
            body["ok"] = true;
            body["version"] = 3;
            Json::Value arr(Json::arrayValue);
            for (const std::string& s : names) arr.append(s);
            body["services"] = std::move(arr);
            rsp["result"] = compact(body);
        } else {
            rsp["result"] =
                "{\"ok\":false,\"error\":\"unknown_call\"}";
        }
        return compact(rsp);
    };

    // Two ext_calls on the caller thread — each blocks the runtime
    // inside ve_call until this pump services its host_call.
    std::atomic<bool> done{false};
    std::string host_reply, services_reply;
    std::thread caller([&] {
        std::string r;
        static_cast<void>(round_trip(
            [&](Json::Value& req) {
                req["kind"] = "ext_call";
                req["ext_id"] = "demo";
                req["fn"] = "ve_demo_hostcall";
                req["args"] = "{}";
            },
            120000, &r));
        host_reply = r;
        r.clear();
        static_cast<void>(round_trip(
            [&](Json::Value& req) {
                req["kind"] = "ext_call";
                req["ext_id"] = "demo";
                req["fn"] = "ve_demo_services";
                req["args"] = "{}";
            },
            120000, &r));
        services_reply = r;
        done.store(true, std::memory_order_release);
    });
    std::string failure;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done.load(std::memory_order_acquire) &&
           failure.empty() &&
           std::chrono::steady_clock::now() < deadline) {
        std::string line;
        if (!pipe.PopCallbackLine(&line)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        Json::Value req;
        if (!parse(line, &req) ||
            req.get("kind", "").asString() != "host_call") {
            continue;
        }
        if (!pipe.WriteCallbackLine(answer(req))) {
            failure = "host_call reply write failed";
        }
    }
    if (!done.load(std::memory_order_acquire)) {
        pipe.KillWorker();   // unstick the parked RoundTrip
    }
    caller.join();
    if (!failure.empty()) {
        return failure;
    }
    if (!done.load(std::memory_order_acquire)) {
        return "ext_calls hung — bridge pump starved";
    }

    // ve_demo_hostcall echoes the ve_call result verbatim: the
    // fabricated project's describe must be inside result_json.
    Json::Value host;
    if (host_reply.empty() || !parse(host_reply, &host)) {
        return "ve_demo_hostcall reply missing/malformed";
    }
    if (host.get("ok", false).asBool()) {
        const std::string result =
            host.get("result_json", "").asString();
        if (result.find("\"width\":160") == std::string::npos ||
            result.find("\"fps_num\":15") == std::string::npos) {
            return "ve_call did not return the describe payload: " +
                   result;
        }
    }
    // ok:false tolerated — stale pack without the export.

    // ve_demo_services echoes the ve.services table.
    Json::Value services;
    if (!services_reply.empty() && parse(services_reply, &services) &&
        services.get("ok", false).asBool()) {
        const std::string result =
            services.get("result_json", "").asString();
        if (result.find("\"version\":3") == std::string::npos ||
            result.find("timeline.info") == std::string::npos ||
            result.find("export.status") == std::string::npos) {
            return "ve.services answer lost the v3 table: " + result;
        }
    }
    // absent/malformed tolerated — ve_demo_services is new in the
    // demo; the transport already proved itself above.

    // Best-effort unload.
    reply.clear();
    static_cast<void>(round_trip(
        [&](Json::Value& r) {
            r["kind"] = "ext_unload";
            r["ext_id"] = "demo";
        },
        15000, &reply));
    return {};
#else
    return {};
#endif
}

// nn_probe.cc — prints the neural/AI backend matrix (which of
// OpenCV / ONNX Runtime / oneDNN / ggml / BLAS / MKL / CUDA / cuDNN
// this build carries and how each is wired). The report itself is
// informational and always passes; for every backend that IS
// compiled in a tiny real computation runs (cv::resize, Ort Env,
// dnnl::sum, a ggml add graph), so a broken link or an unresolvable
// delay-loaded DLL surfaces as a genuine FAIL instead of a silent
// "available" lie.
std::string CheckNnBackends() {
    const std::string report = NnBackendsReport();
    std::fprintf(stdout, "%s", report.c_str());
    std::fflush(stdout);
    for (const NnBackend& b : NnBackends()) {
        // Only backends this TU set actually links get exercised —
        // the report-only rows (BLAS runtime probe, MKL/CUDA/cuDNN
        // configure detections) have nothing to call into.
        if (!b.compiled_in) continue;
        const std::string id(b.id);
        if (id != "opencv" && id != "onnxruntime" && id != "dnnl" &&
            id != "ggml") {
            continue;
        }
        const std::string err = NnBackendSelfTest(b.id);
        if (!err.empty()) {
            return id + " self-test failed: " + err;
        }
    }
    return {};
}

// ---- YA.* GPU-pipeline modules -------------------------------------

// YA.CUDADecode (src/gpu_video): prints the layered availability
// matrix, then proves the module end-to-end on whatever decode path
// this machine supports — CUDA hwaccel when the probe says yes, the
// internal software fallback otherwise. Passing on a GPU-less box IS
// the graceful-degradation contract: the module must stay loadable,
// probe honestly, and still decode.
std::string CheckCudaDecode() {
    const auto& probe = XER::GpuVideo::ProbeCudaDecode();
    std::fprintf(stdout, "%s", probe.Report().c_str());
    if (!FFmpeg::IsAvailable()) {
        return "ffmpeg bridge unavailable";
    }
    // Encode a tiny h264 clip with the software encoder, then decode
    // it back through CudaVideoDecoder — the same Open/NextFrame path
    // a delegated media_decoder would drive.
    const auto clip = SmokeTempDir() / "smoke_cuda_decode.mp4";
    {
        std::error_code ec;
        std::filesystem::remove(clip, ec);
    }
    FFmpeg::EncodeVideoParams params;
    params.output_path = Utf8Path(clip);
    params.codec_name = "libx264";
    params.width = 64;
    params.height = 36;
    params.pixel_format = "bgra";
    params.fps_num = 15;
    std::string err;
    const auto provider = [](std::size_t index,
                             FFmpeg::VideoFrameInfo* f,
                             std::string*) -> bool {
        f->width = 64;
        f->height = 36;
        f->pixel_format = "bgra";
        f->pts = f->best_effort_timestamp =
            static_cast<std::int64_t>(index);
        f->line_sizes = {64 * 4, 0, 0, 0};
        f->data.assign(64 * 36 * 4,
                       static_cast<std::uint8_t>(index));
        return true;
    };
    if (!FFmpeg::EncodeVideoFrameStream(params, 8, provider, &err)) {
        return "could not encode fixture clip: " + err;
    }
    XER::GpuVideo::CudaVideoDecoder decoder;
    if (!decoder.Open(Utf8Path(clip), &err)) {
        return "CudaVideoDecoder.Open failed: " + err;
    }
    video::Frame frame;
    if (!decoder.NextFrame(&frame) || !frame.IsValid() ||
        frame.Width() != 64 || frame.Height() != 36) {
        return "CudaVideoDecoder produced no frame";
    }
    // The IMediaDecoder adapter (MakeGpuDecoder) must honor the same
    // contract — decode through it once to prove the delegation seam,
    // not just the module.
    MediaDecoderPtr delegated = MakeGpuDecoder();
    if (delegated == nullptr ||
        !delegated->Open(Utf8Path(clip), &err)) {
        return "MakeGpuDecoder delegation open failed: " + err;
    }
    std::vector<std::uint8_t> out_bgra;
    int out_w = 0, out_h = 0;
    if (!delegated->NextFrame(&out_bgra, &out_w, &out_h) ||
        out_w != 64 || out_h != 36 || out_bgra.empty()) {
        return "delegated decoder produced no frame";
    }
    std::fprintf(
        stdout,
        "  decode session: hwaccel=%s first_frame_surface=%s\n",
        decoder.ActiveHwaccelName().empty()
            ? "(software)"
            : decoder.ActiveHwaccelName().c_str(),
        decoder.LastFrameSurface() == XER::GpuVideo::FrameSurface::kCuda
            ? "cuda"
            : "cpu");
    return {};
}

// YA.CUDAEncode (src/gpu_video): availability matrix + the codec-name
// resolver contract export_engine relies on — software names pass
// through, "nvenc" aliases resolve to a concrete encoder when the
// device is usable, else fall back to libx264 with a note. Always
// passes (informational + graceful-degradation proof).
std::string CheckCudaEncode() {
    const auto& probe = XER::GpuVideo::ProbeCudaEncode();
    std::fprintf(stdout, "%s", probe.Report().c_str());
    const std::string passthrough =
        XER::GpuVideo::ResolveVideoCodecName("libx264");
    if (passthrough != "libx264") {
        return "software codec name was rewritten";
    }
    std::string note;
    const std::string resolved =
        XER::GpuVideo::ResolveVideoCodecName("nvenc", &note);
    if (resolved.empty()) {
        return "nvenc resolution returned empty";
    }
    if (!probe.Available() && resolved != "libx264") {
        return "nvenc resolved to '" + resolved +
               "' without a usable device";
    }
    std::fprintf(stdout, "  resolve: nvenc -> '%s' (%s)\n",
                 resolved.c_str(),
                 note.empty() ? "device usable" : note.c_str());
    return {};
}

// Recording fake Renderer — proves the YA.FrameRender transport seam:
// which draw call the service issues depends on the attached backend,
// and the pixel payload must arrive intact either way.
class RecordingRenderer final : public XER::UI::Rendering::Renderer {
public:
    int video_id_calls = 0;
    int video_pixels_calls = 0;
    int bgra_calls = 0;
    const void* last_pixels = nullptr;
    bool accept_id_draw = false;   // emulate a store-aware renderer

    void BeginFrame(XER::UI::Layout::Rect) override {}
    void EndFrame() override {}
    void FillRect(XER::UI::Layout::Rect, XER::UI::Theme::Color,
                  float) override {}
    void StrokeRect(XER::UI::Layout::Rect, XER::UI::Theme::Color,
                    float, float) override {}
    void DrawLine(XER::UI::Layout::Point, XER::UI::Layout::Point,
                  XER::UI::Theme::Color, float) override {}
    void DrawText(std::string_view, XER::UI::Layout::Rect,
                  XER::UI::Rendering::TextStyle) override {}
    XER::UI::Layout::Size MeasureText(
        std::string_view,
        XER::UI::Rendering::TextStyle) const override {
        return {};
    }
    bool DrawVideoFrame(std::uint32_t, XER::UI::Layout::Rect, float,
                        float) override {
        ++video_id_calls;
        return accept_id_draw;
    }
    bool DrawVideoPixels(const void* bgra, std::uint32_t, int, int,
                         XER::UI::Layout::Rect, float,
                         float) override {
        ++video_pixels_calls;
        last_pixels = bgra;
        return true;
    }
    bool DrawFrameBgra(const void* bgra, int, int,
                       XER::UI::Layout::Rect, float) override {
        ++bgra_calls;
        last_pixels = bgra;
        return true;
    }
};

// YA.FrameRender (src/frame_render): availability matrix + a REAL
// CPU-path render test (BGRA/RGBA/NV12 normalize + Present through a
// recording renderer), plus the RenderStore seam when this build
// carries it. Always passes on the availability side.
std::string CheckFrameRender() {
    const auto probe = XER::FrameRender::ProbeFrameRender();
    std::fprintf(stdout, "%s", probe.Report().c_str());

    video::Frame bgra(4, 2, video::PixelFormat::BGRA8);
    std::memset(bgra.Data(), 0, bgra.DataSize());
    bgra.Data()[0] = 10;
    bgra.Data()[1] = 20;
    bgra.Data()[2] = 30;
    bgra.Data()[3] = 255;
    std::vector<std::uint8_t> out;
    int w = 0, h = 0;
    if (!XER::FrameRender::Service::Normalize(bgra, &out, &w, &h) ||
        w != 4 || h != 2 || out.size() != 32 || out[0] != 10 ||
        out[1] != 20 || out[2] != 30) {
        return "BGRA normalize failed";
    }
    video::Frame rgba(2, 2, video::PixelFormat::RGBA8);
    rgba.Data()[0] = 30;
    rgba.Data()[1] = 20;
    rgba.Data()[2] = 10;
    rgba.Data()[3] = 255;
    if (!XER::FrameRender::Service::Normalize(rgba, &out, &w, &h) ||
        out[0] != 10 || out[2] != 30) {
        return "RGBA swizzle failed";
    }
    // Neutral NV12 (Y=U=V=128) must land as mid-gray in BGRA.
    video::Frame nv12(4, 4, video::PixelFormat::NV12);
    std::memset(nv12.Data(), 128, nv12.DataSize());
    if (!XER::FrameRender::Service::Normalize(nv12, &out, &w, &h) ||
        out.empty()) {
        return "NV12 normalize failed";
    }
    if (std::abs(static_cast<int>(out[2]) - 128) > 4 ||
        std::abs(static_cast<int>(out[1]) - 128) > 4 ||
        std::abs(static_cast<int>(out[0]) - 128) > 4) {
        return "NV12 neutral gray drifted";
    }

    // Transport seam: with no store attached the pixel path must be
    // chosen; the same call routes through the store slot when one is
    // attached and the renderer accepts the id draw.
    XER::FrameRender::Service service;
    if (!service.Init({})) {
        return "frame-render service init failed";
    }
    RecordingRenderer rec;
    const XER::UI::Layout::Rect dst{0.0f, 0.0f, 4.0f, 2.0f};
    if (!service.Present(rec, bgra, dst, 1.0f) ||
        service.LastBackend() != XER::FrameRender::Backend::kCpu ||
        rec.video_pixels_calls + rec.bgra_calls == 0) {
        return "CPU present path not taken";
    }

    // Store seam — a throwaway segment proves the write+id-draw path
    // (production attaches the UI host's xer.ui.res.<pid> store).
    // render_store_compiled is a runtime flag because the compile-time
    // define lives on the module's objects, not its consumers.
    if (probe.render_store_compiled) {
        auto store = XER::UI::Rendering::RenderStore::Create(
            "xer.ve.smoke.frame_render", 4u * 1024u * 1024u);
        if (store != nullptr) {
            XER::FrameRender::Service stored;
            XER::FrameRender::Options opts;
            opts.store = store.get();
            if (!stored.Init(opts)) {
                return "store-backed init failed";
            }
            RecordingRenderer rec2;
            rec2.accept_id_draw = true;
            if (!stored.Present(rec2, bgra, dst, 1.0f) ||
                stored.LastBackend() !=
                    XER::FrameRender::Backend::kRenderStore ||
                rec2.video_id_calls != 1) {
                return "render_store path not taken";
            }
            const auto view = store->Read(stored.VideoSlot());
            if (view.data == nullptr || view.info.width != 4 ||
                view.info.height != 2 || view.info.payload < 32) {
                return "store slot did not receive the frame";
            }
        }
    }
    std::fprintf(stdout,
                 "  frame_render: cpu path verified%s\n",
                 probe.render_store_compiled ? " (+ store seam)"
                                             : "");
    return {};
}

// YA.AIFindObject (src/content/ai/find_object) — the "AI: Find
// Objects" backend matrix. Informational like nn_backends: prints
// whether ONNX Runtime / OpenCV DNN are linked+loadable and whether a
// detection model is installed. What MUST hold regardless of the
// environment: both backend rows are reported, a missing model path
// fails Load() with a reason instead of crashing, and Detect() on an
// unloaded detector returns empty. When a model IS installed the
// check goes one step further and Detect()s a synthetic BGRA frame —
// a real inference path exercised end to end.
std::string CheckAiFindObject() {
    namespace FindObj = XER::AI::FindObject;

    // ---- availability matrix (always printed) ----
    const std::vector<FindObj::BackendInfo> backends =
        FindObj::Backends();
    std::fprintf(stdout, "  ai_find_object backends:\n%s",
                 FindObj::BackendReport().c_str());
    const std::string model = Ai::FindObjectModel();
    std::fprintf(stdout, "  ai_find_object model: %s\n",
                 model.empty() ? "<none>" : model.c_str());
    std::fflush(stdout);
    if (backends.size() != 2) {
        return "Backends() must report both rows (onnxruntime, "
               "opencv_dnn)";
    }

    // ---- unavailable paths must degrade, never crash ----
    FindObj::Detector detector;
    if (detector.IsLoaded() || !detector.BackendId().empty()) {
        return "fresh Detector reports loaded";
    }
    std::string err;
    if (detector.Load("definitely/missing/model.onnx", "", "", &err)) {
        return "Load() accepted a nonexistent model";
    }
    if (err.empty()) {
        return "Load() failed a missing model without a reason";
    }
    // Synthetic BGRA8 frame (160x120 opaque gray) — Detect on an
    // unloaded detector is a documented empty result.
    std::vector<std::uint8_t> bgra(160 * 120 * 4, 0x80);
    for (std::size_t p = 3; p < bgra.size(); p += 4) bgra[p] = 0xFF;
    if (!detector.Detect(bgra.data(), 160, 120, 160 * 4).empty()) {
        return "unloaded Detector returned detections";
    }

    // ---- real-model path: only when the user installed one ----
    if (model.empty() || FindObj::ActiveBackendId().empty()) {
        return {};   // nothing to run inference on — graceful skip
    }
    if (!detector.Load(model, Ai::FindObjectLabels(model), "", &err)) {
        return "installed model failed to load: " + err;
    }
    // A uniform frame may legitimately produce zero detections — the
    // contract is "returns a vector, does not crash".
    static_cast<void>(detector.Detect(bgra.data(), 160, 120, 160 * 4));
    std::fprintf(stdout,
                 "  ai_find_object inference ran on backend '%s'\n",
                 detector.BackendId().c_str());
    std::fflush(stdout);
    return {};
}

// AI tag layer (ai_tags.h + Clip::tags persistence). Verifies the
// graceful-degradation calls (no model / un-decodable clip fail with
// reasons, never crash) and the project-store round trip that keeps
// ClipTag ranges on the clip.
std::string CheckAiTags() {
    // ---- graceful-degradation contract ----
    Clip clip;
    clip.id = "tag-scan";
    clip.source_path = "definitely/missing/media.mp4";
    clip.duration = 5.0;
    clip.in_point = 0.0;
    std::vector<ClipTag> tags;
    std::string err;
    if (Ai::ScanClipForObjects(clip, "", "", Ai::TagScanParams{},
                               nullptr, nullptr, &tags, &err)) {
        return "ScanClipForObjects succeeded with no model";
    }
    if (err.empty() || !tags.empty()) {
        return "no-model scan did not report a reason / left tags";
    }
    Clip generated = MakeGeneratedClip("gen", 0, 0.0, 1.0,
                                       SourceKind::ColorBars,
                                       "smpte75");
    if (Ai::ScanClipForObjects(generated, "x.onnx", "",
                               Ai::TagScanParams{}, nullptr, nullptr,
                               &tags, &err)) {
        return "ScanClipForObjects scanned a generated clip";
    }

    // ---- Clip::tags serialization round trip ----
    Project project;
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    Clip tagged;
    tagged.id = "tagged";
    tagged.source_path = "tagged.mp4";   // need not exist for the store
    tagged.track = 0;
    tagged.timeline_start = 0.0;
    tagged.in_point = 4.0;
    tagged.duration = 8.0;
    ClipTag t0;
    t0.start = 5.0;
    t0.end = 7.5;
    t0.label = "person";
    t0.confidence = 0.83f;
    ClipTag t1;
    t1.start = 9.0;
    t1.end = 10.25;
    t1.label = "car";
    t1.confidence = 0.42f;
    tagged.tags = {t0, t1};
    project.clips.push_back(tagged);

    const std::string path =
        Utf8Path(SmokeTempDir() / "smoke_ai_tags.xerproj");
    if (!ProjectStore::Save(project, path, &err)) {
        return "Save failed: " + err;
    }
    Project loaded;
    if (!ProjectStore::Load(path, &loaded, &err)) {
        return "Load failed: " + err;
    }
    const Clip* back = FindClip(loaded, "tagged");
    if (back == nullptr || back->tags.size() != 2) {
        return "clip tags did not survive the round trip";
    }
    const ClipTag& a = back->tags[0];
    const ClipTag& b = back->tags[1];
    if (a.start != 5.0 || a.end != 7.5 || a.label != "person" ||
        std::abs(a.confidence - 0.83f) > 1e-6f ||
        b.start != 9.0 || b.end != 10.25 || b.label != "car" ||
        std::abs(b.confidence - 0.42f) > 1e-6f) {
        return "tag fields did not survive the round trip";
    }
    // And the inverse: a tag-free clip stays tag-free.
    Clip plain = MakeGeneratedClip("plain", 0, 20.0, 1.0,
                                   SourceKind::ColorMatte, "#112233");
    project.clips.push_back(plain);
    if (!ProjectStore::Save(project, path, &err) ||
        !ProjectStore::Load(path, &loaded, &err)) {
        return "second round trip failed: " + err;
    }
    if (const Clip* p = FindClip(loaded, "plain");
        p == nullptr || !p->tags.empty()) {
        return "tag-free clip gained tags in the round trip";
    }
    return {};
}

using CheckFn = std::string (*)();
struct CheckEntry {
    const char* name;
    CheckFn fn;
};

// Order is part of the contract --smoke-list advertises. Cheap pure
// checks first; the heavyweight boot and FFmpeg export go last.
constexpr CheckEntry kChecks[] = {
    {"timecode", &CheckTimecode},
    {"sequence_presets", &CheckSequencePresets},
    {"timeline_model", &CheckTimelineModel},
    {"timeline_history", &CheckTimelineHistory},
    {"source_edit", &CheckSourceEdit},
    {"keyboard_map", &CheckKeyboardMap},
    {"export_presets", &CheckExportPresets},
    {"generated_source", &CheckGeneratedSource},
    {"text_style_ext", &CheckTextStyleExt},
    {"compositor_render", &CheckCompositorRender},
    {"screenshot_png", &CheckScreenshotPng},
    {"ui_screenshot_diff", &CheckUiScreenshotDiff},
    {"curves_lens", &CheckCurvesAndLens},
    {"preview_quality", &CheckPreviewQualityScale},
    {"js_fx", &CheckJsFx},
    {"audio_mixer", &CheckAudioMixer},
    {"audio_sync", &CheckAudioSync},
    {"project_store", &CheckProjectStore},
    {"generated_layers", &CheckGeneratedLayers},
    {"project_autosave", &CheckProjectAutosave},
    {"media_catalog", &CheckMediaCatalog},
    {"records_store", &CheckRecordsStore},
    {"project_manager", &CheckProjectManager},
    {"media_import_filter", &CheckMediaImportFilter},
    {"lut_manager", &CheckLutManager},
    {"url_import", &CheckUrlImport},
    {"probe_classification", &CheckProbeClassification},
    {"poster_cache_lru", &CheckPosterCacheLru},
    {"media_worker", &CheckMediaWorker},
    {"sync_worker", &CheckSyncMediaWorker},
    {"fx_worker", &CheckFxScriptWorker},
    {"ext_runtime", &CheckExtRuntime},
    {"ext_bridge", &CheckExtBridge},
    {"ext_xejslib", &CheckExtXejslib},
    {"ve_core_v3", &CheckVeCoreV3},
    {"assets_icons", &CheckAssetsIcons},
    {"extensions_panel", &CheckExtensionsPanelData},
    {"ext_xsjo", &CheckExtXsjo},
    {"nn_backends", &CheckNnBackends},
    {"ai_find_object", &CheckAiFindObject},
    {"ai_tags", &CheckAiTags},
    {"cuda_decode", &CheckCudaDecode},
    {"cuda_encode", &CheckCudaEncode},
    {"frame_render", &CheckFrameRender},
    {"app_boot", &CheckAppBoot},
    {"export_render", &CheckExportRender},
};

}  // namespace

std::vector<std::string> CheckNames() {
    std::vector<std::string> names;
    names.reserve(std::size(kChecks));
    for (const CheckEntry& entry : kChecks) {
        names.emplace_back(entry.name);
    }
    return names;
}

int RunAll() {
    // Keep libav chatter off stderr — encoder stats are noise here;
    // real encode failures still surface through the check's reason.
    FFmpeg::SetLogLevel(FFmpeg::LogLevel::kError);

    // Hard cap: a hung check (deadlocked decoder, stuck V8 isolate)
    // still takes the process down non-zero instead of blocking a
    // harness forever. Cancellable: once the suite reports its result
    // the flag is armed so a slow CRT/FFmpeg teardown past the
    // deadline can no longer fire a spurious _Exit after
    // "SMOKE RESULT OK". Detached — normal exit kills it first.
    std::atomic<bool> watchdog_done{false};
    std::thread([&watchdog_done] {
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(kWatchdogSeconds);
        while (std::chrono::steady_clock::now() < deadline &&
               !watchdog_done.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
        }
        if (watchdog_done.load(std::memory_order_acquire)) {
            return;
        }
        std::fprintf(stdout,
                     "SMOKE watchdog FAIL exceeded %d seconds\n",
                     kWatchdogSeconds);
        std::fflush(stdout);
        std::_Exit(1);
    }).detach();

    int failures = 0;
    for (const CheckEntry& entry : kChecks) {
        const auto started = std::chrono::steady_clock::now();
        const std::string reason = entry.fn();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
        if (reason.empty()) {
            std::fprintf(stdout, "SMOKE %s OK\n", entry.name);
        } else {
            ++failures;
            std::fprintf(stdout, "SMOKE %s FAIL %s\n", entry.name,
                         reason.c_str());
        }
        std::fprintf(stderr, "  (%lld ms)\n",
                     static_cast<long long>(elapsed));
        std::fflush(stdout);
    }
    std::fprintf(stdout, "SMOKE RESULT %s\n",
                 failures == 0 ? "OK" : "FAIL");
    std::fflush(stdout);
    // Disarm the watchdog before teardown — the result line is out.
    watchdog_done.store(true, std::memory_order_release);
    return failures == 0 ? 0 : 1;
}

}  // namespace Smoke
}  // namespace XER::VideoEditor
