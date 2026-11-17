// Timecode + sequence-preset + keyboard-map checks for the
// XER_VideoEditor_Tests runner. Non-drop scope per timecode.h: the
// frame field counts the nominal integer rate; 30000/1001 maps to
// nominal 30.
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "keyboard_map.h"
#include "sequence_preset.h"
#include "timecode.h"
#include "timeline_model.h"

namespace {
using namespace XER::VideoEditor;

// Frames -> timecode -> frames must be an exact identity at every
// supported rate; parse->seconds additionally recovers frames*den/num.
void CheckRoundTrip(int num, int den, std::int64_t frames) {
    const double seconds = static_cast<double>(frames) *
                           static_cast<double>(den) / num;
    const std::string tc = FormatTimecode(seconds, num, den);
    XER_VE_CHECK(ParseToFrames(tc, num, den) == frames);
    double parsed = -1.0;
    XER_VE_CHECK(ParseTimecode(tc, num, den, &parsed));
    const double parsed_frames = parsed * num / den;
    XER_VE_CHECK(std::abs(parsed_frames - frames) < 1e-6);
}

}  // namespace

int XER_VideoEditor_RunTimecodeTests() {
    // -- FormatTimecode: shape + per-rate spot values -----------------
    {
        XER_VE_CHECK(FormatTimecode(0.0, 30, 1) == "00:00:00:00");
        XER_VE_CHECK(FormatTimecode(1.5, 30, 1) == "00:00:01:15");
        XER_VE_CHECK(FormatTimecode(1.5, 24, 1) == "00:00:01:12");
        XER_VE_CHECK(FormatTimecode(1.0, 25, 1) == "00:00:01:00");
        XER_VE_CHECK(3661.5 * 30 == 109845.0);   // sanity for next line
        XER_VE_CHECK(FormatTimecode(3661.5, 30, 1) == "01:01:01:15");
        XER_VE_CHECK(FormatTimecode(1.5, 60, 1) == "00:00:01:30");
        // Hours stay two-digit-padded and keep counting past 24.
        XER_VE_CHECK(FormatTimecode(25.0 * 3600.0, 30, 1) ==
                     "25:00:00:00");
        // Non-finite / negative input and a broken rate clamp to zero.
        XER_VE_CHECK(FormatTimecode(-5.0, 30, 1) == "00:00:00:00");
        XER_VE_CHECK(FormatTimecode(
                         std::numeric_limits<double>::infinity(), 30,
                         1) == "00:00:00:00");
        XER_VE_CHECK(FormatTimecode(10.0, 0, 1) == "00:00:00:00");
        XER_VE_CHECK(FormatTimecode(10.0, 30, 0) == "00:00:00:00");
    }

    // -- frame-boundary rounding --------------------------------------
    {
        // Nearest-frame quantization at 30 fps.
        XER_VE_CHECK(SecondsToFrames(0.0, 30, 1) == 0);
        XER_VE_CHECK(SecondsToFrames(1.0 / 30.0, 30, 1) == 1);
        XER_VE_CHECK(SecondsToFrames(0.05, 30, 1) == 2);   // 1.5 fr -> 2
        XER_VE_CHECK(SecondsToFrames(-1.0, 30, 1) == -30);
        XER_VE_CHECK(SecondsToFrames(1.0, 30000, 1001) == 30);
        // Just under/over a frame edge lands on the same code.
        XER_VE_CHECK(FormatTimecode(0.999, 30, 1) == "00:00:01:00");
        XER_VE_CHECK(FormatTimecode(1.001, 30, 1) == "00:00:01:00");
        XER_VE_CHECK(FormatTimecode(0.016, 30, 1) == "00:00:00:00");
        XER_VE_CHECK(FormatTimecode(0.017, 30, 1) == "00:00:00:01");
    }

    // -- format/parse round-trips at 24/25/30/60 ----------------------
    {
        const int kRates[][2] = {{24, 1}, {25, 1}, {30, 1}, {60, 1}};
        for (const auto& rate : kRates) {
            const int fps = rate[0];
            for (const std::int64_t frames :
                 {std::int64_t{0}, std::int64_t{1},
                  std::int64_t{fps - 1}, std::int64_t{fps},
                  std::int64_t{12 * fps + 7},
                  std::int64_t{3599 * fps + 3},
                  std::int64_t{fps * 86400 + fps / 2}}) {
                CheckRoundTrip(rate[0], rate[1], frames);
            }
        }
        // NTSC non-drop: nominal 30, true 30000/1001 — a one-minute
        // label is 1800 frames == 60.06 wall seconds.
        XER_VE_CHECK(ParseToFrames("00:01:00:00", 30000, 1001) == 1800);
        double ntsc = 0.0;
        XER_VE_CHECK(ParseTimecode("00:01:00:00", 30000, 1001, &ntsc));
        XER_VE_CHECK(std::abs(ntsc - 60.06) < 1e-6);
        XER_VE_CHECK(FormatTimecode(60.06, 30000, 1001) ==
                     "00:01:00:00");
    }

    // -- ParseTimecode: hours-optional form + rejections --------------
    {
        double s = -1.0;
        // "MM:SS:FF" — (2*60+3)*30+12 = 3702 frames == 123.4 s.
        XER_VE_CHECK(ParseTimecode("02:03:12", 30, 1, &s));
        XER_VE_CHECK(std::abs(s - 123.4) < 1e-6);
        XER_VE_CHECK(ParseToFrames("02:03:12", 30, 1) == 3702);
        // Single-digit fields are fine; null out-param only validates.
        XER_VE_CHECK(ParseTimecode("1:2:3", 30, 1, nullptr));
        XER_VE_CHECK(ParseToFrames("1:2:3", 30, 1) == 62 * 30 + 3);
        // FF must stay under the nominal rate (per-rate bound).
        XER_VE_CHECK(!ParseTimecode("00:00:00:30", 30, 1, &s));
        XER_VE_CHECK(ParseTimecode("00:00:00:29", 30, 1, &s));
        XER_VE_CHECK(!ParseTimecode("00:00:00:24", 24, 1, &s));
        XER_VE_CHECK(ParseTimecode("00:00:00:23", 24, 1, &s));
        // MM/SS bounds.
        XER_VE_CHECK(!ParseTimecode("12:60:00:00", 30, 1, &s));
        XER_VE_CHECK(!ParseTimecode("12:34:60:00", 30, 1, &s));
        // Malformed: empty, garbage, wrong field counts, signs,
        // whitespace, drop-frame ';', empty fields, >2-digit fields.
        const char* kBad[] = {
            "", "abc", "1:2", "1:2:3:4:5", "-01:00:00:00",
            "00:00:00:-1", " 00:00:00:00", "00:00:00:00 ",
            "00: 00:00:00", "0::0:0", "00;00;00;00", "123:45:56",
            "00:00:00:00:00", "00:00",
        };
        for (const char* bad : kBad) {
            XER_VE_CHECK(!ParseTimecode(bad, 30, 1, &s));
            XER_VE_CHECK(ParseToFrames(bad, 30, 1) == -1);
        }
        // A bad rate parses nothing.
        XER_VE_CHECK(!ParseTimecode("00:00:00:00", 0, 1, &s));
        XER_VE_CHECK(ParseToFrames("00:00:00:00", 30, 0) == -1);
    }

    // -- FormatDuration (bin display) ----------------------------------
    {
        XER_VE_CHECK(FormatDuration(0.0) == "0:00:00.000");
        XER_VE_CHECK(FormatDuration(-3.0) == "0:00:00.000");
        XER_VE_CHECK(FormatDuration(5.25) == "0:00:05.250");
        XER_VE_CHECK(FormatDuration(60.0) == "0:01:00.000");
        XER_VE_CHECK(FormatDuration(3600.0) == "1:00:00.000");
        XER_VE_CHECK(FormatDuration(3723.5) == "1:02:03.500");
        // Millisecond rounding carries into the seconds/minutes.
        XER_VE_CHECK(FormatDuration(59.9996) == "0:01:00.000");
        XER_VE_CHECK(FormatDuration(0.0014) == "0:00:00.001");
    }

    // -- SequencePreset sanity ----------------------------------------
    {
        const auto presets = BuiltinSequencePresets();
        XER_VE_CHECK(presets.size() == 11);
        std::set<std::string> names;
        for (const SequencePreset& p : presets) {
            XER_VE_CHECK(!p.name.empty());
            XER_VE_CHECK(names.insert(p.name).second);   // unique
            XER_VE_CHECK(p.w >= 256 && p.w <= 8192);
            XER_VE_CHECK(p.h >= 256 && p.h <= 8192);
            XER_VE_CHECK(p.fps_num >= 1 && p.fps_den >= 1);
            const double fps =
                static_cast<double>(p.fps_num) / p.fps_den;
            XER_VE_CHECK(fps >= 20.0 && fps <= 70.0);
        }

        Project project;
        project.width = 1920;
        project.height = 1080;
        project.fps_num = 30;
        project.fps_den = 1;
        const SequencePreset uhd{"UHD 4K 30", 3840, 2160, 30, 1};
        XER_VE_CHECK(ApplySequencePreset(uhd, &project));
        XER_VE_CHECK(project.width == 3840 && project.height == 2160);
        XER_VE_CHECK(project.fps_num == 30 && project.fps_den == 1);
        // Null project and incomplete presets are rejected untouched.
        XER_VE_CHECK(!ApplySequencePreset(uhd, nullptr));
        XER_VE_CHECK(!ApplySequencePreset(SequencePreset{}, &project));
        XER_VE_CHECK(project.width == 3840 && project.height == 2160);

        // FindPreset: every builtin resolves by dims+rate (hit);
        // non-builtin shapes come back nameless (miss = custom).
        for (const SequencePreset& p : presets) {
            const SequencePreset found =
                FindPreset(p.w, p.h, p.fps_num, p.fps_den);
            XER_VE_CHECK(found.name == p.name);
        }
        XER_VE_CHECK(FindPreset(1920, 1080, 30, 1).name ==
                     "Full HD 1080p 30");
        XER_VE_CHECK(FindPreset(3840, 2160, 30, 1).name == "UHD 4K 30");
        XER_VE_CHECK(FindPreset(640, 480, 30, 1).name.empty());
        XER_VE_CHECK(FindPreset(3840, 2160, 60, 1).name.empty());
    }

    // -- Keyboard map sanity -------------------------------------------
    {
        const auto& shortcuts = EditorShortcuts();
        XER_VE_CHECK(!shortcuts.empty());
        std::set<std::string> groups;
        for (const ShortcutEntry& e : shortcuts) {
            XER_VE_CHECK(!e.group.empty() && !e.keys.empty() &&
                         !e.action.empty());
            groups.insert(e.group);
        }
        XER_VE_CHECK(groups.count("Tools") == 1);
        XER_VE_CHECK(groups.count("Playback") == 1);
        XER_VE_CHECK(groups.count("Markers") == 1);
        XER_VE_CHECK(groups.count("Edit") == 1);
        XER_VE_CHECK(groups.count("View") == 1);
    }

    return XER::VideoEditor::Test::Failures();
}
