// .xerproj JSON v1 store suite — save/load round-trip fidelity
// (doubles, floats, UTF-8, enum strings), version gating, lenient
// optional fields vs strict required fields, transition self-heal via
// DropInvalidTransitions, and the crash-safe tmp+rename save.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <json/json.h>

#include "video_editor_test_util.h"
#include "helper/path_utils.h"
#include "project_store.h"
#include "timeline_model.h"

namespace {
using namespace XER::VideoEditor;
namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers

bool NearlyEq(double a, double b) { return std::fabs(a - b) <= 1e-9; }
bool NearlyEq(float a, float b) {
    return std::fabs(static_cast<double>(a) - static_cast<double>(b)) <=
           1e-6;
}

// Field-by-field project equality — same comparator shape as the
// history suite (kept local; anonymous-namespace helpers can't be
// shared across TUs).
bool ClipsEqual(const Clip& a, const Clip& b) {
    if (a.id != b.id || a.source_path != b.source_path ||
        a.track != b.track || a.muted != b.muted) {
        return false;
    }
    if (!NearlyEq(a.timeline_start, b.timeline_start) ||
        !NearlyEq(a.in_point, b.in_point) ||
        !NearlyEq(a.duration, b.duration) ||
        !NearlyEq(a.gain_db, b.gain_db) ||
        !NearlyEq(a.fade_in, b.fade_in) ||
        !NearlyEq(a.fade_out, b.fade_out)) {
        return false;
    }
    const Transform& x = a.transform;
    const Transform& y = b.transform;
    if (!NearlyEq(x.offset_x, y.offset_x) ||
        !NearlyEq(x.offset_y, y.offset_y) ||
        !NearlyEq(x.scale, y.scale) ||
        !NearlyEq(x.rotation_deg, y.rotation_deg) ||
        !NearlyEq(x.opacity, y.opacity)) {
        return false;
    }
    if (a.effects.size() != b.effects.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.effects.size(); ++i) {
        if (a.effects[i].type != b.effects[i].type ||
            a.effects[i].params.size() != b.effects[i].params.size()) {
            return false;
        }
        for (std::size_t j = 0; j < a.effects[i].params.size(); ++j) {
            if (!NearlyEq(a.effects[i].params[j], b.effects[i].params[j])) {
                return false;
            }
        }
    }
    return true;
}

bool ProjectsEqual(const Project& a, const Project& b) {
    auto fail = [](const char* what) {
        std::fprintf(stderr, "ProjectsEqual mismatch: %s\n", what);
        return false;
    };
    if (a.width != b.width || a.height != b.height ||
        a.fps_num != b.fps_num || a.fps_den != b.fps_den ||
        a.bg_color != b.bg_color) {
        return fail("project header");
    }
    if (a.tracks.size() != b.tracks.size()) {
        return fail("track count");
    }
    for (std::size_t i = 0; i < a.tracks.size(); ++i) {
        const Track& x = a.tracks[i];
        const Track& y = b.tracks[i];
        if (x.kind != y.kind || x.name != y.name || x.muted != y.muted ||
            x.hidden != y.hidden || !NearlyEq(x.gain_db, y.gain_db)) {
            return fail("track fields");
        }
    }
    if (a.clips.size() != b.clips.size()) {
        return fail("clip count");
    }
    for (std::size_t i = 0; i < a.clips.size(); ++i) {
        if (!ClipsEqual(a.clips[i], b.clips[i])) {
            std::fprintf(stderr, "ProjectsEqual mismatch: clip %zu\n", i);
            return false;
        }
    }
    if (a.transitions.size() != b.transitions.size()) {
        return fail("transition count");
    }
    for (std::size_t i = 0; i < a.transitions.size(); ++i) {
        const Transition& x = a.transitions[i];
        const Transition& y = b.transitions[i];
        if (x.from_clip != y.from_clip || x.to_clip != y.to_clip ||
            x.type != y.type || !NearlyEq(x.duration, y.duration)) {
            return fail("transition fields");
        }
    }
    return true;
}

// "D:/медиа/видео 🎬.mp4" as raw UTF-8 bytes — hex escapes keep the
// test independent of the compiler's source charset.
const char* kUtf8Source =
    "D:/\xD0\xBC\xD0\xB5\xD0\xB4\xD0\xB8\xD0\xB0/"
    "\xD0\xB2\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE \xF0\x9F\x8E\xAC.mp4";

// Round-trip fixture: 2 tracks (video+audio), 3 clips covering every
// Clip field, 1 non-default-type transition between adjacent clips.
Project MakeRoundTripProject() {
    Project p;
    p.width = 1280;
    p.height = 720;
    p.fps_num = 24;
    p.fps_den = 1;
    p.bg_color = {16, 32, 48, 255};
    p.tracks.push_back(
        Track{TrackKind::Video, "V1", false, false, false, 2.5f});
    p.tracks.push_back(
        Track{TrackKind::Audio, "A1", true, true, false, -6.0f});

    Clip a;
    a.id = "clip-a";
    a.source_path = "D:/media/a.mp4";
    a.track = 0;
    a.timeline_start = 0.0;
    a.in_point = 1.5;
    a.duration = 10.0;
    a.muted = false;
    a.gain_db = -3.0f;
    a.fade_in = 0.5;
    a.fade_out = 0.25;
    a.transform = {0.1f, -0.05f, 1.5f, 90.0f, 0.75f};
    a.effects = {{"box_blur", {4.0f, 2.5f}}, {"vignette", {}}};
    p.clips.push_back(a);

    Clip b;
    b.id = "clip-b";
    b.source_path = "D:/media/b.mp4";
    b.track = 0;
    b.timeline_start = 10.0;   // adjacent to clip-a's end
    b.in_point = 0.0;
    b.duration = 8.0;
    b.muted = true;
    b.gain_db = 6.0f;
    b.fade_out = 1.0;
    p.clips.push_back(b);

    Clip c;
    c.id = "clip-c";
    c.source_path = "D:/media/c.mp3";
    c.track = 1;
    c.timeline_start = 2.0;
    c.in_point = 0.5;
    c.duration = 12.0;
    p.clips.push_back(c);

    p.transitions.push_back(
        Transition{"clip-a", "clip-b", Transition::Type::WipeLeft, 1.5});
    return p;
}

std::string Utf8Path(const std::filesystem::path& p) { return Path::ToUtf8(p); }

bool WriteTextFile(const std::string& utf8_path, const std::string& text) {
    std::ofstream out(Path::FromUtf8(utf8_path),
                      std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    return static_cast<bool>(out);
}

Json::Value ReadProjectJson(const std::string& utf8_path) {
    std::ifstream in(Path::FromUtf8(utf8_path), std::ios::binary);
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    Json::parseFromStream(builder, in, &root, &errors);
    return root;
}

void WriteProjectJson(const std::string& utf8_path, const Json::Value& root) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    WriteTextFile(utf8_path, Json::writeString(builder, root));
}

// Writes `json` to <dir>/<name> and Loads it — the leniency/self-heal
// cases all follow mutate→save→load.
bool TryLoadJson(const std::string& dir, const char* name,
                 const Json::Value& json, Project* out, std::string* err) {
    const std::string path = dir + "/" + name;
    WriteProjectJson(path, json);
    return ProjectStore::Load(path, out, err);
}

}  // namespace

int XER_VideoEditor_RunStoreTests() {
    const std::string dir = Utf8Path(Test::TempDir());
    std::string err;

    // -- 1. full round-trip -------------------------------------------
    const Project original = MakeRoundTripProject();
    const std::string rt_path = dir + "/roundtrip.xerproj";
    {
        XER_VE_CHECK(ProjectStore::Save(original, rt_path, &err));
        Project loaded;
        XER_VE_CHECK(ProjectStore::Load(rt_path, &loaded, &err));
        XER_VE_CHECK(ProjectsEqual(original, loaded));
        // tmp+rename: target exists, no stray .tmp left behind
        XER_VE_CHECK(std::filesystem::exists(Path::FromUtf8(rt_path)));
        XER_VE_CHECK(
            !std::filesystem::exists(Path::FromUtf8(rt_path + ".tmp")));
    }

    // -- 2. UTF-8 clip source + non-ASCII .xerproj filename -------------
    {
        Project p;
        p.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip c;
        c.id = "utf8";
        c.source_path = kUtf8Source;
        c.track = 0;
        c.timeline_start = 0.0;
        c.in_point = 0.0;
        c.duration = 1.0;
        p.clips.push_back(c);

        // "utf8-🎬.xerproj" — exercises Path::FromUtf8 on the file path.
        const std::string upath =
            dir + "/utf8-\xF0\x9F\x8E\xAC.xerproj";
        XER_VE_CHECK(ProjectStore::Save(p, upath, &err));
        Project loaded;
        std::string lerr;
        XER_VE_CHECK(ProjectStore::Load(upath, &loaded, &lerr));
        XER_VE_CHECK(loaded.clips.size() == 1);
        if (loaded.clips.size() == 1) {
            // byte-identical compare — no locale involved
            XER_VE_CHECK(loaded.clips[0].source_path == kUtf8Source);
        }
    }

    // -- 3. version gate -------------------------------------------------
    {
        const Json::Value base = ReadProjectJson(rt_path);

        Json::Value v2 = base;
        v2["version"] = ProjectStore::kVersion + 1;
        const std::string v2_path = dir + "/version2.xerproj";
        WriteProjectJson(v2_path, v2);
        Project out;
        std::string lerr;
        XER_VE_CHECK(!ProjectStore::Load(v2_path, &out, &lerr));
        XER_VE_CHECK(lerr.find("version") != std::string::npos);

        Json::Value v0 = base;
        v0.removeMember("version");
        const std::string v0_path = dir + "/version0.xerproj";
        WriteProjectJson(v0_path, v0);
        Project loaded;
        XER_VE_CHECK(ProjectStore::Load(v0_path, &loaded, &lerr));
        XER_VE_CHECK(ProjectsEqual(original, loaded));
    }

    // -- 4. robustness ----------------------------------------------------
    {
        Project out;
        std::string lerr;

        XER_VE_CHECK(!ProjectStore::Load(dir + "/no_such_file.xerproj", &out,
                                         &lerr));
        XER_VE_CHECK(!lerr.empty());

        const std::string garbage = dir + "/garbage.xerproj";
        WriteTextFile(garbage, "this is { not json ]]]");
        XER_VE_CHECK(!ProjectStore::Load(garbage, &out, &lerr));
        XER_VE_CHECK(!lerr.empty());

        const std::string arr_root = dir + "/array_root.xerproj";
        WriteTextFile(arr_root, "[1,2,3]");
        XER_VE_CHECK(!ProjectStore::Load(arr_root, &out, &lerr));
        XER_VE_CHECK(!lerr.empty());

        const std::string str_root = dir + "/string_root.xerproj";
        WriteTextFile(str_root, "\"just a string\"");
        XER_VE_CHECK(!ProjectStore::Load(str_root, &out, &lerr));
        XER_VE_CHECK(!lerr.empty());
    }

    // -- 5. leniency: optional fields default; required fields strict -----
    {
        const Json::Value base = ReadProjectJson(rt_path);

        auto expect_fail = [&](Json::Value v, const char* name) {
            Project p;
            std::string lerr;
            const bool ok = TryLoadJson(dir, name, v, &p, &lerr);
            XER_VE_CHECK(!ok);
            XER_VE_CHECK(!lerr.empty());
        };

        // Absent optional fields → struct defaults.
        {
            Json::Value v = base;
            v["tracks"][0].removeMember("name");
            v["tracks"][0].removeMember("hidden");
            v["clips"][0].removeMember("effects");
            v["clips"][0].removeMember("transform");
            v["clips"][0].removeMember("muted");
            v["clips"][0].removeMember("gain_db");
            v["clips"][0].removeMember("fade_in");
            v["clips"][0].removeMember("fade_out");
            Project p;
            std::string lerr;
            XER_VE_CHECK(
                TryLoadJson(dir, "lenient_optional.xerproj", v, &p, &lerr));
            XER_VE_CHECK(p.tracks.size() == 2);
            XER_VE_CHECK(p.clips.size() == 3);
            if (p.tracks.size() == 2 && p.clips.size() == 3) {
                XER_VE_CHECK(p.tracks[0].name.empty());
                XER_VE_CHECK(p.tracks[0].hidden == false);
                XER_VE_CHECK(p.clips[0].effects.empty());
                XER_VE_CHECK(p.clips[0].muted == false);
                XER_VE_CHECK(p.clips[0].gain_db == 0.0f);
                XER_VE_CHECK(p.clips[0].fade_in == 0.0);
                XER_VE_CHECK(p.clips[0].fade_out == 0.0);
                const Transform& t = p.clips[0].transform;
                XER_VE_CHECK(t.offset_x == 0.0f && t.offset_y == 0.0f &&
                             t.scale == 1.0f && t.rotation_deg == 0.0f &&
                             t.opacity == 1.0f);
            }
        }

        // Required / range-checked fields → hard errors.
        {
            Json::Value v = base;
            v["clips"][0].removeMember("id");
            expect_fail(v, "err_no_id.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["id"] = "";
            expect_fail(v, "err_empty_id.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][1]["id"] = v["clips"][0]["id"];
            expect_fail(v, "err_dup_id.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0].removeMember("source");
            expect_fail(v, "err_no_source.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["track"] = 99;
            expect_fail(v, "err_track_range.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["track"] = -1;
            expect_fail(v, "err_track_neg.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["track"] = "0";   // wrong JSON type
            expect_fail(v, "err_track_type.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["duration"] = -2.0;
            expect_fail(v, "err_neg_duration.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["duration"] = 0.0;
            expect_fail(v, "err_zero_duration.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["timeline_start"] = -1.0;
            expect_fail(v, "err_neg_start.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["in_point"] = -0.5;
            expect_fail(v, "err_neg_inpoint.xerproj");
        }
        {
            Json::Value v = base;
            v["transitions"][0]["type"] = "warp_drive";
            expect_fail(v, "err_bad_transition_type.xerproj");
        }
        // Overlap on the same track violates the model's core
        // invariant → hard error (corrupt primary data, like dup ids).
        {
            Json::Value v = base;
            v["clips"][1]["timeline_start"] = 5.0;  // overlaps clip-a
            expect_fail(v, "err_overlap.xerproj");
        }
        // Adjacency (end == start) is NOT overlap → loads fine; the
        // fixture's clip-c already overlaps clip-a in time on a
        // DIFFERENT track, proving the track check too.
        {
            Json::Value v = base;
            v["clips"][1]["timeline_start"] = 10.0;  // == clip-a end
            Project p;
            std::string lerr;
            XER_VE_CHECK(
                TryLoadJson(dir, "ok_adjacent.xerproj", v, &p, &lerr));
            XER_VE_CHECK(p.clips.size() == 3);
            XER_VE_CHECK(p.transitions.size() == 1);  // a->b still valid
        }
    }

    // -- 6. self-heal: broken transitions drop, load still succeeds ------
    {
        const Json::Value base = ReadProjectJson(rt_path);

        auto expect_healed = [&](Json::Value v, const char* name) {
            Project p;
            std::string lerr;
            XER_VE_CHECK(TryLoadJson(dir, name, v, &p, &lerr));
            XER_VE_CHECK(p.clips.size() == 3);
            XER_VE_CHECK(p.transitions.empty());
        };

        // endpoint clip id does not exist
        {
            Json::Value v = base;
            v["transitions"][0]["to"] = "ghost-clip";
            expect_healed(v, "heal_missing_clip.xerproj");
        }
        // clips exist but are no longer adjacent (gap opened)
        {
            Json::Value v = base;
            v["clips"][1]["timeline_start"] = 50.0;
            expect_healed(v, "heal_nonadjacent.xerproj");
        }
        // endpoints on different tracks
        {
            Json::Value v = base;
            v["transitions"][0]["to"] = "clip-c";   // audio track
            expect_healed(v, "heal_different_track.xerproj");
        }
        // "from" member absent → lenient empty string → dropped
        {
            Json::Value v = base;
            v["transitions"][0].removeMember("from");
            expect_healed(v, "heal_no_from.xerproj");
        }
        // non-positive transition duration → dropped (AddTransition
        // requires duration > 0; DropInvalidTransitions doesn't check
        // it, so the store must — self-heal, not a hard error)
        {
            Json::Value v = base;
            v["transitions"][0]["duration"] = 0.0;
            expect_healed(v, "heal_duration_zero.xerproj");
        }
        {
            Json::Value v = base;
            v["transitions"][0]["duration"] = -1.5;
            expect_healed(v, "heal_duration_neg.xerproj");
        }

        // Same-time markers self-heal into one — the model keys on
        // time (AddMarker merges, RemoveMarker hits the first), so a
        // hand-edited duplicate pair would be unaddressable. The LAST
        // file entry wins, matching AddMarker's merge.
        {
            Json::Value v = base;
            Json::Value markers(Json::arrayValue);
            Json::Value first(Json::objectValue);
            first["time"] = 4.0;
            first["name"] = "first";
            Json::Value second(Json::objectValue);
            second["time"] = 4.0;
            second["name"] = "second";
            markers.append(first);
            markers.append(second);
            v["markers"] = markers;
            Project p;
            std::string lerr;
            XER_VE_CHECK(
                TryLoadJson(dir, "heal_dup_markers.xerproj", v, &p,
                            &lerr));
            XER_VE_CHECK(p.markers.size() == 1);
            if (p.markers.size() == 1) {
                XER_VE_CHECK(p.markers[0].name == "second");
            }
        }
    }

    // -- 7. empty project round-trips -------------------------------------
    {
        const Project empty;
        const std::string ep = dir + "/empty.xerproj";
        XER_VE_CHECK(ProjectStore::Save(empty, ep, &err));
        Project loaded;
        XER_VE_CHECK(ProjectStore::Load(ep, &loaded, &err));
        XER_VE_CHECK(ProjectsEqual(empty, loaded));
        XER_VE_CHECK(loaded.tracks.empty() && loaded.clips.empty() &&
                     loaded.transitions.empty());
        XER_VE_CHECK(loaded.width == 1920 && loaded.height == 1080 &&
                     loaded.fps_num == 30 && loaded.fps_den == 1);
    }

    // -- 8. deeply nested JSON: parser stack-limit must not throw -------
    {
        // jsoncpp throws Json::RuntimeError once readValue() recursion
        // exceeds its stack limit (~1000 levels). Load's contract is
        // never-throw — a crafted .xerproj must come back as a normal
        // false + error, not an exception escaping to a noexcept UI
        // caller. (If Load ever regresses to throwing, this check
        // crashes the suite instead of failing cleanly.)
        const std::string deep_path = dir + "/deep_nesting.xerproj";
        std::string deep;
        deep.reserve(2000 * 6);
        for (int i = 0; i < 2000; ++i) {
            deep += "{\"a\":";
        }
        deep += "1";
        for (int i = 0; i < 2000; ++i) {
            deep += "}";
        }
        XER_VE_CHECK(WriteTextFile(deep_path, deep));
        Project out;
        std::string lerr;
        XER_VE_CHECK(!ProjectStore::Load(deep_path, &out, &lerr));
        XER_VE_CHECK(!lerr.empty());
    }

    return 0;
}
