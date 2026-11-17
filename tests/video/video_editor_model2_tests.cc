// .xerproj v2 serialization suite — round-trip fidelity for every
// field the v2 format adds over the v1 coverage in the store suite:
// playback_rate, blend_mode, pan, link_group, masks (rect + ellipse +
// inverted + feather), keyframes (all props, every easing incl.
// Hold), markers incl. color, work_in/work_out, track.locked,
// audio_fx (path/name/enabled/params), and ClipSource kind+param for
// every generated kind. Negative cases exercise the strict-typed
// field readers (unknown enum strings, out-of-range values); the
// model op owns the keyframe time invariant, so out-of-range keyframe
// rejection is asserted through SetClipKeyframes.
#include <cmath>
#include <cstddef>
#include <cstdint>
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
namespace Path = XER::Helper::Path;

bool NearlyEq(double a, double b) { return std::fabs(a - b) <= 1e-9; }
bool NearlyEq(float a, float b) {
    return std::fabs(static_cast<double>(a) - static_cast<double>(b)) <=
           1e-6;
}

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

bool WriteTextFile(const std::string& utf8_path,
                   const std::string& text) {
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

void WriteProjectJson(const std::string& utf8_path,
                      const Json::Value& root) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    WriteTextFile(utf8_path, Json::writeString(builder, root));
}

// Writes `json` to <dir>/<name> and Loads it — every negative case is
// mutate->save->load like the store suite.
bool TryLoadJson(const std::string& dir, const char* name,
                 const Json::Value& json, Project* out,
                 std::string* err) {
    const std::string path = dir + "/" + name;
    WriteProjectJson(path, json);
    return ProjectStore::Load(path, out, err);
}

// One project carrying every v2 field at a non-default value.
Project MakeV2Project() {
    Project p;
    p.width = 640;
    p.height = 360;
    p.fps_num = 24;
    p.fps_den = 1;
    p.bg_color = {8, 16, 24, 255};
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
    p.tracks.push_back(
        Track{TrackKind::Audio, "A1", true, false, true, -3.0f});
    p.tracks.push_back(
        Track{TrackKind::Video, "V2", false, false, true, 0.0f});

    Clip a;
    a.id = "clip-a";
    a.source_path = "D:/media/a.mp4";
    a.track = 0;
    a.timeline_start = 0.0;
    a.in_point = 1.5;
    a.duration = 10.0;
    a.playback_rate = 1.5;
    a.muted = true;
    a.gain_db = -3.0f;
    a.pan = -0.5f;
    a.fade_in = 0.25;
    a.fade_out = 0.5;
    a.transform = {0.1f, -0.2f, 1.25f, 45.0f, 0.75f};
    a.blend_mode = BlendMode::Multiply;
    a.link_group = "grp1";
    a.masks = {
        ClipMask{MaskShape::Rect, 0.25f, 0.25f, 0.5f, 0.5f, 0.10f,
                 false},
        ClipMask{MaskShape::Ellipse, 0.60f, 0.60f, 0.30f, 0.40f, 0.20f,
                 true}};
    a.keyframes[KeyProp::PosX] = {
        {0.0, 0.0f, KeyEase::Linear},
        {10.0, 1.0f, KeyEase::Hold}};
    a.keyframes[KeyProp::PosY] = {
        {0.0, 0.2f, KeyEase::EaseIn}};
    a.keyframes[KeyProp::Scale] = {
        {0.0, 1.0f, KeyEase::EaseOut},
        {5.0, 2.0f, KeyEase::Linear}};
    a.keyframes[KeyProp::Rotation] = {
        {0.0, 45.0f, KeyEase::EaseInOut}};
    a.keyframes[KeyProp::Opacity] = {
        {0.0, 1.0f, KeyEase::Linear},
        {5.0, 0.5f, KeyEase::Hold}};
    a.keyframes[KeyProp::GainDb] = {
        {0.0, -6.0f, KeyEase::EaseIn}};
    a.audio_fx = {
        ClipAudioFx{"D:/plugins/eq.vst3", "EQ", true,
                    {{0u, 0.5f}, {7u, 1.0f}}},
        ClipAudioFx{"D:/plugins/comp.vst3", "Comp", false, {}}};
    p.clips.push_back(a);

    Clip b;
    b.id = "clip-b";
    b.source_path = "D:/media/b.mp4";
    b.track = 0;
    b.timeline_start = 10.0;   // adjacent to clip-a -> transition
    b.duration = 8.0;
    b.blend_mode = BlendMode::Screen;
    p.clips.push_back(b);

    Clip matte;
    matte.id = "matte";
    matte.track = 0;
    matte.timeline_start = 18.0;
    matte.duration = 5.0;
    matte.source.kind = SourceKind::ColorMatte;
    matte.source.param = "#FF8000";
    p.clips.push_back(matte);

    Clip bars;
    bars.id = "bars";
    bars.track = 0;
    bars.timeline_start = 23.0;
    bars.duration = 5.0;
    bars.source.kind = SourceKind::ColorBars;
    bars.source.param = "smpte75";
    p.clips.push_back(bars);

    Clip title;
    title.id = "title";
    title.track = 0;
    title.timeline_start = 28.0;
    title.duration = 5.0;
    title.source.kind = SourceKind::Text;
    title.source.param = "Hello XER";
    p.clips.push_back(title);

    Clip au;
    au.id = "au";
    au.source_path = "D:/media/a.mp3";
    au.track = 1;              // on the LOCKED audio track — data only
    au.timeline_start = 0.0;
    au.duration = 30.0;
    p.clips.push_back(au);

    Clip v2;
    v2.id = "v2";
    v2.source_path = "D:/media/v2.mp4";
    v2.track = 2;              // on the LOCKED video track
    v2.timeline_start = 0.0;
    v2.duration = 5.0;
    v2.link_group = "grp1";
    p.clips.push_back(v2);

    p.transitions.push_back(Transition{"clip-a", "clip-b",
                                       Transition::Type::SlideLeft,
                                       1.5});
    p.markers = {Marker{2.5, "intro", {255, 64, 0, 255}},
                 Marker{7.0, "mid", {0, 200, 255, 128}}};
    p.work_in = 2.0;
    p.work_out = 8.0;
    return p;
}

bool CheckKeyframes(const Clip& clip, KeyProp prop, double t0,
                    float v0, KeyEase e0, double t1, float v1,
                    KeyEase e1, std::size_t count) {
    const auto it = clip.keyframes.find(prop);
    if (it == clip.keyframes.end() || it->second.size() != count) {
        return false;
    }
    const auto& keys = it->second;
    if (!NearlyEq(keys[0].time, t0) || !NearlyEq(keys[0].value, v0) ||
        keys[0].easing != e0) {
        return false;
    }
    if (count > 1 &&
        (!NearlyEq(keys[1].time, t1) || !NearlyEq(keys[1].value, v1) ||
         keys[1].easing != e1)) {
        return false;
    }
    return true;
}

}  // namespace

int XER_VideoEditor_RunModel2Tests() {
    const std::string dir = Utf8Path(Test::TempDir()) + "/model2";
    std::filesystem::create_directories(Path::FromUtf8(dir));
    std::string err;

    const Project original = MakeV2Project();
    const std::string rt_path = dir + "/v2_roundtrip.xerproj";

    // -- full v2 round-trip -------------------------------------------
    Project loaded;
    {
        XER_VE_CHECK(ProjectStore::Save(original, rt_path, &err));
        XER_VE_CHECK(std::filesystem::exists(Path::FromUtf8(rt_path)));
        XER_VE_CHECK(ProjectStore::Load(rt_path, &loaded, &err));

        // version stamp is v2
        const Json::Value root = ReadProjectJson(rt_path);
        XER_VE_CHECK(root["version"].asInt() == ProjectStore::kVersion);

        // canvas + work area + markers
        XER_VE_CHECK(loaded.width == 640 && loaded.height == 360 &&
                     loaded.fps_num == 24 && loaded.fps_den == 1);
        XER_VE_CHECK(loaded.bg_color[0] == 8 &&
                     loaded.bg_color[3] == 255);
        XER_VE_CHECK(NearlyEq(loaded.work_in, 2.0) &&
                     NearlyEq(loaded.work_out, 8.0));
        XER_VE_CHECK(loaded.markers.size() == 2);
        XER_VE_CHECK(NearlyEq(loaded.markers[0].time, 2.5) &&
                     loaded.markers[0].name == "intro" &&
                     loaded.markers[0].color[0] == 255 &&
                     loaded.markers[0].color[1] == 64 &&
                     loaded.markers[0].color[3] == 255);
        XER_VE_CHECK(NearlyEq(loaded.markers[1].time, 7.0) &&
                     loaded.markers[1].color[2] == 255 &&
                     loaded.markers[1].color[3] == 128);

        // tracks: kind/name/muted + the v2 locked flag + gain
        XER_VE_CHECK(loaded.tracks.size() == 3);
        XER_VE_CHECK(loaded.tracks[0].kind == TrackKind::Video &&
                     !loaded.tracks[0].locked);
        XER_VE_CHECK(loaded.tracks[1].kind == TrackKind::Audio &&
                     loaded.tracks[1].muted &&
                     loaded.tracks[1].locked &&
                     NearlyEq(loaded.tracks[1].gain_db, -3.0f));
        XER_VE_CHECK(loaded.tracks[2].locked);

        // clip-a: every v2 field
        const Clip* a = FindClip(loaded, "clip-a");
        XER_VE_CHECK(a != nullptr);
        if (a != nullptr) {
            XER_VE_CHECK(NearlyEq(a->playback_rate, 1.5));
            XER_VE_CHECK(NearlyEq(a->pan, -0.5f));
            XER_VE_CHECK(a->blend_mode == BlendMode::Multiply);
            XER_VE_CHECK(a->link_group == "grp1");
            XER_VE_CHECK(a->muted && NearlyEq(a->gain_db, -3.0f) &&
                         NearlyEq(a->fade_in, 0.25) &&
                         NearlyEq(a->fade_out, 0.5));
            XER_VE_CHECK(NearlyEq(a->transform.offset_x, 0.1f) &&
                         NearlyEq(a->transform.rotation_deg, 45.0f) &&
                         NearlyEq(a->transform.opacity, 0.75f));
            // masks: rect + ellipse, inverted flag, feather
            XER_VE_CHECK(a->masks.size() == 2);
            if (a->masks.size() == 2) {
                const ClipMask& r = a->masks[0];
                XER_VE_CHECK(r.shape == MaskShape::Rect && !r.inverted);
                XER_VE_CHECK(NearlyEq(r.cx, 0.25f) &&
                             NearlyEq(r.w, 0.5f) &&
                             NearlyEq(r.feather, 0.10f));
                const ClipMask& e = a->masks[1];
                XER_VE_CHECK(e.shape == MaskShape::Ellipse &&
                             e.inverted);
                XER_VE_CHECK(NearlyEq(e.cy, 0.60f) &&
                             NearlyEq(e.h, 0.40f) &&
                             NearlyEq(e.feather, 0.20f));
            }
            // keyframes: all six props, every easing incl. Hold
            XER_VE_CHECK(a->keyframes.size() == 6);
            XER_VE_CHECK(CheckKeyframes(*a, KeyProp::PosX, 0.0, 0.0f,
                                        KeyEase::Linear, 10.0, 1.0f,
                                        KeyEase::Hold, 2));
            XER_VE_CHECK(CheckKeyframes(*a, KeyProp::PosY, 0.0, 0.2f,
                                        KeyEase::EaseIn, 0.0, 0.0f,
                                        KeyEase::Linear, 1));
            XER_VE_CHECK(CheckKeyframes(*a, KeyProp::Scale, 0.0, 1.0f,
                                        KeyEase::EaseOut, 5.0, 2.0f,
                                        KeyEase::Linear, 2));
            XER_VE_CHECK(CheckKeyframes(
                *a, KeyProp::Rotation, 0.0, 45.0f, KeyEase::EaseInOut,
                0.0, 0.0f, KeyEase::Linear, 1));
            XER_VE_CHECK(CheckKeyframes(*a, KeyProp::Opacity, 0.0,
                                        1.0f, KeyEase::Linear, 5.0,
                                        0.5f, KeyEase::Hold, 2));
            XER_VE_CHECK(CheckKeyframes(*a, KeyProp::GainDb, 0.0,
                                        -6.0f, KeyEase::EaseIn, 0.0,
                                        0.0f, KeyEase::Linear, 1));
            // audio_fx chain: order, path/name/enabled/params
            XER_VE_CHECK(a->audio_fx.size() == 2);
            if (a->audio_fx.size() == 2) {
                const ClipAudioFx& eq = a->audio_fx[0];
                XER_VE_CHECK(eq.plugin_path == "D:/plugins/eq.vst3" &&
                             eq.name == "EQ" && eq.enabled);
                XER_VE_CHECK(eq.params.size() == 2 &&
                             NearlyEq(eq.params.at(0u), 0.5f) &&
                             NearlyEq(eq.params.at(7u), 1.0f));
                const ClipAudioFx& comp = a->audio_fx[1];
                XER_VE_CHECK(comp.plugin_path ==
                                 "D:/plugins/comp.vst3" &&
                             comp.name == "Comp" && !comp.enabled &&
                             comp.params.empty());
            }
        }

        // clip-b: non-default blend mode of another enum value
        const Clip* b = FindClip(loaded, "clip-b");
        XER_VE_CHECK(b != nullptr &&
                     b->blend_mode == BlendMode::Screen);

        // generated kinds: kind + param round-trip, source_path stays
        // empty (the empty "source" member is legal for non-Media)
        const Clip* matte = FindClip(loaded, "matte");
        XER_VE_CHECK(matte != nullptr &&
                     matte->source.kind == SourceKind::ColorMatte &&
                     matte->source.param == "#FF8000" &&
                     matte->source_path.empty());
        const Clip* bars = FindClip(loaded, "bars");
        XER_VE_CHECK(bars != nullptr &&
                     bars->source.kind == SourceKind::ColorBars &&
                     bars->source.param == "smpte75");
        const Clip* title = FindClip(loaded, "title");
        XER_VE_CHECK(title != nullptr &&
                     title->source.kind == SourceKind::Text &&
                     title->source.param == "Hello XER");

        // locked-track clips and the link_group on v2
        const Clip* v2 = FindClip(loaded, "v2");
        XER_VE_CHECK(v2 != nullptr && v2->track == 2 &&
                     v2->link_group == "grp1");

        // v2 transition type survives the round-trip
        XER_VE_CHECK(loaded.transitions.size() == 1);
        XER_VE_CHECK(loaded.transitions[0].type ==
                         Transition::Type::SlideLeft &&
                     NearlyEq(loaded.transitions[0].duration, 1.5));
    }

    // -- generated clip loads WITHOUT a "source" member ---------------
    {
        const Json::Value base = ReadProjectJson(rt_path);
        Json::Value v = base;
        v["clips"][2].removeMember("source");   // the matte clip
        Project p;
        std::string lerr;
        XER_VE_CHECK(TryLoadJson(dir, "gen_no_source.xerproj", v, &p,
                                 &lerr));
        const Clip* matte = FindClip(p, "matte");
        XER_VE_CHECK(matte != nullptr &&
                     matte->source.kind == SourceKind::ColorMatte &&
                     matte->source_path.empty());
    }

    // -- negative: unknown / out-of-range v2 values -------------------
    {
        const Json::Value base = ReadProjectJson(rt_path);
        auto expect_fail = [&](Json::Value v, const char* name) {
            Project p;
            std::string lerr;
            XER_VE_CHECK(!TryLoadJson(dir, name, v, &p, &lerr));
            XER_VE_CHECK(!lerr.empty());
        };

        {
            Json::Value v = base;
            v["clips"][2]["source_kind"] = "hologram";
            expect_fail(v, "err_source_kind.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["blend_mode"] = "warp_drive";
            expect_fail(v, "err_blend_mode.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["pan"] = 2.0;   // out of [-1, 1]
            expect_fail(v, "err_pan_range.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["playback_rate"] = 0.0;   // must be > 0
            expect_fail(v, "err_rate_zero.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["masks"][0]["shape"] = "triangle";
            expect_fail(v, "err_mask_shape.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["keyframes"]["position_x"] =
                v["clips"][0]["keyframes"]["pos_x"];
            v["clips"][0]["keyframes"].removeMember("pos_x");
            expect_fail(v, "err_key_prop.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["keyframes"]["pos_x"][0]["ease"] = "bounce";
            expect_fail(v, "err_key_ease.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["audio_fx"][0]["path"] = "";
            expect_fail(v, "err_fx_path.xerproj");
        }
        {
            Json::Value v = base;
            v["clips"][0]["audio_fx"][0]["params"]["notanumber"] = 0.5;
            expect_fail(v, "err_fx_param_id.xerproj");
        }
    }

    // -- keyframe out-of-range: the MODEL owns the [0, duration]
    //    invariant (SetClipKeyframes validates through
    //    ValidateClipPlacement; the store round-trips clip data
    //    verbatim) ---------------------------------------------------
    {
        const Clip* a = FindClip(loaded, "clip-a");
        XER_VE_CHECK(a != nullptr);
        if (a != nullptr) {
            const std::size_t key_count = a->keyframes.size();
            std::vector<Keyframe> over = {
                {a->duration + 1.0, 0.5f, KeyEase::Linear}};
            XER_VE_CHECK(!SetClipKeyframes(loaded, "clip-a",
                                           KeyProp::PosX, over, &err));
            // rejected: the loaded keyframe set is untouched
            XER_VE_CHECK(FindClip(loaded, "clip-a")->keyframes.size() ==
                         key_count);
            // in-range replacement is accepted on the loaded clip
            std::vector<Keyframe> ok = {
                {0.0, 0.0f, KeyEase::Linear},
                {a->duration, 1.0f, KeyEase::Hold}};
            XER_VE_CHECK(SetClipKeyframes(loaded, "clip-a",
                                          KeyProp::PosX, ok, &err));
        }
    }

    // -- empty v2 fields stay absent: no link_group/masks/audio_fx
    //    keys are emitted for default clips ---------------------------
    {
        Project minimal;
        minimal.tracks.push_back(Track{TrackKind::Video, "V1"});
        Clip c;
        c.id = "plain";
        c.source_path = "D:/media/x.mp4";
        c.track = 0;
        c.duration = 5.0;
        minimal.clips.push_back(c);
        const std::string mp = dir + "/minimal.xerproj";
        XER_VE_CHECK(ProjectStore::Save(minimal, mp, &err));
        const Json::Value root = ReadProjectJson(mp);
        const Json::Value& cj = root["clips"][0];
        XER_VE_CHECK(!cj.isMember("blend_mode"));
        XER_VE_CHECK(!cj.isMember("source_kind"));
        XER_VE_CHECK(!cj.isMember("link_group"));
        XER_VE_CHECK(!cj.isMember("masks"));
        XER_VE_CHECK(!cj.isMember("keyframes"));
        XER_VE_CHECK(!cj.isMember("audio_fx"));
        XER_VE_CHECK(!root.isMember("markers"));
        XER_VE_CHECK(!root.isMember("work_in"));
        // ...and defaults come back on load.
        Project back;
        XER_VE_CHECK(ProjectStore::Load(mp, &back, &err));
        XER_VE_CHECK(back.clips.size() == 1 &&
                     back.clips[0].blend_mode == BlendMode::Normal &&
                     back.clips[0].source.kind == SourceKind::Media &&
                     NearlyEq(back.clips[0].playback_rate, 1.0) &&
                     back.clips[0].pan == 0.0f &&
                     back.clips[0].link_group.empty());
    }

    return XER::VideoEditor::Test::Failures();
}
