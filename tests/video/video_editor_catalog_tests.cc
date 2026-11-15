// MediaCatalog suite — SQLite-backed media library: schema creation,
// probe+upsert dedup by canonical path (COLLATE NOCASE), audio-only
// rows, Find/List/Remove, poster lifecycle, UTF-8 paths. FFmpeg-backed
// cases self-skip when the bridge is unavailable (same gating as the
// poster suite); fixtures are synthesized in %TEMP% via
// EncodeVideoFrameStream / EncodeAudioFrames — no binary assets.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <json/json.h>

#include "video_editor_test_util.h"
#include "helper/path_utils.h"
#include "media_catalog.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
namespace Test = XER::VideoEditor::Test;
using XER::VideoEditor::CatalogEntry;
using XER::VideoEditor::MediaCatalog;

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

// ---- fixtures (same generators as the poster suite) -------------------

constexpr int kVideoW = 64;
constexpr int kVideoH = 48;
constexpr int kVideoFps = 15;
constexpr int kVideoFrameCount = 30;  // 2 s @ 15 fps

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioTotalSamples = kAudioRate;  // 1 s

void FillVideoFrame(XER::Bridge::FFmpeg::VideoFrameInfo* frame,
                    std::size_t index) {
    frame->width = kVideoW;
    frame->height = kVideoH;
    frame->pixel_format = "bgra";
    frame->line_sizes = {kVideoW * 4, 0, 0, 0};
    frame->data.assign(static_cast<std::size_t>(kVideoW) * kVideoH * 4u,
                       0);
    const int bar_x = static_cast<int>(index * 3) % kVideoW;
    for (int y = 0; y < kVideoH; ++y) {
        for (int x = 0; x < kVideoW; ++x) {
            const bool bar = x >= bar_x && x < bar_x + 8;
            const std::size_t o =
                (static_cast<std::size_t>(y) * kVideoW + x) * 4u;
            frame->data[o + 0] = bar ? 40 : 30;    // B
            frame->data[o + 1] = bar ? 120 : 60;   // G
            frame->data[o + 2] = bar ? 220 : 90;   // R
            frame->data[o + 3] = 255;              // A
        }
    }
}

bool MakeVideoFile(const std::string& utf8_path, std::string* err) {
    XER::Bridge::FFmpeg::EncodeVideoParams params;
    params.output_path = utf8_path;
    params.width = kVideoW;
    params.height = kVideoH;
    params.pixel_format = "bgra";
    params.fps_num = kVideoFps;
    params.fps_den = 1;
    params.bit_rate = 200000;
    for (const char* codec : {"libx264", "mpeg4"}) {
        params.codec_name = codec;
        err->clear();
        if (XER::Bridge::FFmpeg::EncodeVideoFrameStream(
                params, kVideoFrameCount,
                [](std::size_t index,
                   XER::Bridge::FFmpeg::VideoFrameInfo* out,
                   std::string*) {
                    FillVideoFrame(out, index);
                    return true;
                },
                err)) {
            return true;
        }
    }
    return false;
}

void FillSine(XER::Bridge::FFmpeg::AudioFrameInfo* frame,
              std::size_t first_sample, int sample_count) {
    frame->sample_rate = kAudioRate;
    frame->channels = kAudioChannels;
    frame->sample_count = sample_count;
    frame->planar = false;
    frame->sample_format = "s16";
    frame->data.assign(
        static_cast<std::size_t>(sample_count) * kAudioChannels * 2u, 0);
    auto* pcm = reinterpret_cast<std::int16_t*>(frame->data.data());
    for (int i = 0; i < sample_count; ++i) {
        const double t =
            static_cast<double>(first_sample + i) / kAudioRate;
        const std::int16_t v = static_cast<std::int16_t>(
            12000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * t));
        pcm[i * kAudioChannels + 0] = v;
        pcm[i * kAudioChannels + 1] = v;
    }
}

std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> MakeSineFrames(
    int total_samples) {
    std::vector<XER::Bridge::FFmpeg::AudioFrameInfo> frames;
    for (int done = 0; done < total_samples;) {
        const int n = std::min(1024, total_samples - done);
        XER::Bridge::FFmpeg::AudioFrameInfo f;
        FillSine(&f, static_cast<std::size_t>(done), n);
        frames.push_back(std::move(f));
        done += n;
    }
    return frames;
}

bool MakeAudioFile(const std::string& utf8_path, std::string* err) {
    XER::Bridge::FFmpeg::EncodeAudioParams params;
    params.output_path = utf8_path;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kAudioRate;
    params.channels = kAudioChannels;
    params.sample_format = "s16";
    return XER::Bridge::FFmpeg::EncodeAudioFrames(
        params, MakeSineFrames(kAudioTotalSamples), err);
}

// ---- small helpers ------------------------------------------------------

bool HasPngMagic(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    char magic[4] = {};
    in.read(magic, 4);
    return in.gcount() == 4 && magic[0] == '\x89' && magic[1] == 'P' &&
           magic[2] == 'N' && magic[3] == 'G';
}

bool EndsWith(const std::string& text, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return text.size() >= n &&
           text.compare(text.size() - n, n, suffix) == 0;
}

// probe_json must be a parseable JSON object carrying the probed fields.
bool ProbeJsonLooksSane(const std::string& probe_json) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream in(probe_json);
    if (!Json::parseFromStream(builder, in, &root, &errors) ||
        !root.isObject()) {
        return false;
    }
    return root.isMember("width") && root.isMember("height") &&
           root.isMember("duration_seconds") &&
           root.isMember("file_size_bytes");
}

std::size_t CountRows(const std::vector<CatalogEntry>& entries) {
    return entries.size();
}

}  // namespace

int XER_VideoEditor_RunCatalogTests() {
    std::string err;

    // -- 0. DefaultDbPath: non-empty, ends in catalog.db ----------------
    {
        const std::string def = MediaCatalog::DefaultDbPath();
        XER_VE_CHECK(!def.empty());
        XER_VE_CHECK(EndsWith(def, "catalog.db"));
    }

    // -- 1. Open creates parent dirs + schema; second open sees empty ---
    {
        // Nested path that does not exist yet — proves create_directories.
        const auto db_path =
            Test::TempDir() / "catalog_open" / "nested" / "dir" /
            "catalog.db";
        const std::string db_utf8 = Utf8Path(db_path);
        {
            MediaCatalog cat(db_utf8);
            XER_VE_CHECK(cat.Open(&err));
            XER_VE_CHECK(cat.IsOpen());
            XER_VE_CHECK(std::filesystem::exists(db_path));
            XER_VE_CHECK(CountRows(cat.List(&err)) == 0);
        }
        {
            // Reopen on the same file: schema is IF NOT EXISTS — a no-op.
            MediaCatalog cat(db_utf8);
            XER_VE_CHECK(cat.Open(&err));
            XER_VE_CHECK(cat.IsOpen());
            XER_VE_CHECK(cat.DbPath() == db_utf8);
            XER_VE_CHECK(CountRows(cat.List(&err)) == 0);
        }
    }

    // Cases 2-8 need probed media fixtures — skip cleanly without FFmpeg.
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        return 0;
    }

    const auto dir = Test::TempDir() / "catalog";
    // The catalog db + posters persist in %TEMP% between runs — wipe the
    // dir first or row-count checks see previous-run leftovers.
    std::error_code rm_ec;
    std::filesystem::remove_all(dir, rm_ec);
    if (rm_ec) {
        std::fprintf(stderr, "note: cannot clean catalog dir: %s\n",
                     rm_ec.message().c_str());
        return 0;
    }
    std::filesystem::create_directories(dir);
    const auto video_path = dir / "clip.mp4";
    const auto wav_path = dir / "audio_only.wav";

    if (!MakeVideoFile(Utf8Path(video_path), &err)) {
        std::fprintf(stderr,
                     "note: catalog video fixture encode failed: %s\n",
                     err.c_str());
        return 0;
    }
    const bool wav_ok = MakeAudioFile(Utf8Path(wav_path), &err);
    if (!wav_ok) {
        std::fprintf(stderr,
                     "note: catalog audio fixture encode failed: %s\n",
                     err.c_str());
    }

    const std::string video_utf8 = Utf8Path(video_path);
    const std::string wav_utf8 = Utf8Path(wav_path);

    const auto db_path = dir / "media.db";
    const std::string db_utf8 = Utf8Path(db_path);

    MediaCatalog cat(db_utf8);
    XER_VE_CHECK(cat.Open(&err));
    XER_VE_CHECK(cat.IsOpen());

    // -- 2. Import mp4: probed fields + poster ---------------------------
    std::int64_t video_id = 0;
    std::string perr;
    XER_VE_CHECK(cat.Import(video_utf8, &video_id, true, &perr));
    XER_VE_CHECK(video_id > 0);

    CatalogEntry video_entry;
    XER_VE_CHECK(cat.FindById(video_id, &video_entry, &perr));
    XER_VE_CHECK(video_entry.width == kVideoW);
    XER_VE_CHECK(video_entry.height == kVideoH);
    XER_VE_CHECK(video_entry.duration_seconds > 0.0);
    XER_VE_CHECK(video_entry.fps > 0.0);
    XER_VE_CHECK(video_entry.has_audio || !video_entry.video_codec.empty());
    XER_VE_CHECK(video_entry.file_size_bytes > 0);
    XER_VE_CHECK(video_entry.mtime_unix > 0);
    XER_VE_CHECK(video_entry.added_at_unix > 0);
    XER_VE_CHECK(!video_entry.probe_json.empty());
    XER_VE_CHECK(ProbeJsonLooksSane(video_entry.probe_json));
    XER_VE_CHECK(video_entry.HasVideo());
    // Canonical stored path: absolute, UTF-8, resolves to the fixture.
    XER_VE_CHECK(!video_entry.path.empty());
    XER_VE_CHECK(video_entry.path.find("clip.mp4") != std::string::npos);

    // extract_poster=true -> posters/<id>.png written next to the DB.
    XER_VE_CHECK(!video_entry.poster_path.empty());
    XER_VE_CHECK(EndsWith(video_entry.poster_path, ".png"));
    const auto poster_fs = Path::FromUtf8(video_entry.poster_path);
    XER_VE_CHECK(std::filesystem::exists(poster_fs));
    XER_VE_CHECK(HasPngMagic(poster_fs));
    const std::int64_t first_added_at = video_entry.added_at_unix;

    // -- 3. Re-import same path: same id, added_at preserved, 1 row ------
    {
        std::int64_t again_id = 0;
        XER_VE_CHECK(cat.Import(video_utf8, &again_id, true, &perr));
        XER_VE_CHECK(again_id == video_id);
        XER_VE_CHECK(CountRows(cat.List(&perr)) == 1);
        CatalogEntry again;
        XER_VE_CHECK(cat.FindById(video_id, &again, &perr));
        XER_VE_CHECK(again.added_at_unix == first_added_at);
        // Probe fields still refreshed (same file -> same values).
        XER_VE_CHECK(again.width == kVideoW);
        XER_VE_CHECK(!again.poster_path.empty());
    }

    // -- 4. Audio-only wav: width 0, no poster ---------------------------
    std::int64_t wav_id = 0;
    if (wav_ok) {
        XER_VE_CHECK(cat.Import(wav_utf8, &wav_id, true, &perr));
        XER_VE_CHECK(wav_id > 0);
        XER_VE_CHECK(wav_id != video_id);
        CatalogEntry wav_entry;
        XER_VE_CHECK(cat.FindById(wav_id, &wav_entry, &perr));
        XER_VE_CHECK(wav_entry.width == 0);
        XER_VE_CHECK(wav_entry.height == 0);
        XER_VE_CHECK(!wav_entry.HasVideo());
        XER_VE_CHECK(wav_entry.has_audio);
        XER_VE_CHECK(wav_entry.video_codec.empty());
        XER_VE_CHECK(!wav_entry.audio_codec.empty());
        XER_VE_CHECK(wav_entry.duration_seconds > 0.0);
        XER_VE_CHECK(wav_entry.file_size_bytes > 0);
        // extract_poster skipped for audio-only -> poster_path stays empty.
        XER_VE_CHECK(wav_entry.poster_path.empty());
    }

    // -- 5. FindByPath (case-variant) / FindById / List ordering ---------
    {
        // ASCII case-fold through COLLATE NOCASE — pure DB lookup, works
        // on every platform even though only Windows FS is case-insensitive.
        std::string upper = video_utf8;
        for (char& c : upper) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        CatalogEntry by_path;
        XER_VE_CHECK(cat.FindByPath(upper, &by_path, &perr));
        XER_VE_CHECK(by_path.id == video_id);

        CatalogEntry by_id;
        XER_VE_CHECK(cat.FindById(video_id, &by_id, &perr));
        XER_VE_CHECK(by_id.path == video_entry.path);

        const auto all = cat.List(&perr);
        XER_VE_CHECK(all.size() == (wav_ok ? 2u : 1u));
        if (!all.empty()) {
            XER_VE_CHECK(all.front().id == video_id);
        }
        if (wav_ok && all.size() == 2u) {
            XER_VE_CHECK(all[1].id == wav_id);
            XER_VE_CHECK(all[0].id < all[1].id);
        }
    }

    // -- 6. Remove: row gone, poster file deleted -------------------------
    {
        const std::string poster_utf8 = video_entry.poster_path;
        XER_VE_CHECK(cat.Remove(video_id, true, &perr));
        CatalogEntry gone;
        XER_VE_CHECK(!cat.FindById(video_id, &gone, &perr));
        if (!poster_utf8.empty()) {
            XER_VE_CHECK(
                !std::filesystem::exists(Path::FromUtf8(poster_utf8)));
        }
        const auto remaining = cat.List(&perr);
        XER_VE_CHECK(remaining.size() == (wav_ok ? 1u : 0u));
    }

    // -- 7. Failure paths ---------------------------------------------------
    {
        const auto missing = dir / "does_not_exist.mp4";
        std::int64_t bad_id = -1;
        XER_VE_CHECK(!cat.Import(Utf8Path(missing), &bad_id, true, &perr));
        XER_VE_CHECK(!perr.empty());
        XER_VE_CHECK(bad_id == -1);  // untouched on failure

        // A text file probes as "no decodable streams" -> Import false.
        const auto text_path = dir / "notes.txt";
        {
            std::ofstream out(Path::FromUtf8(Utf8Path(text_path)),
                              std::ios::binary | std::ios::trunc);
            out << "this is not a media file\n";
        }
        std::int64_t text_id = -1;
        perr.clear();
        XER_VE_CHECK(!cat.Import(Utf8Path(text_path), &text_id, true,
                                 &perr));
        XER_VE_CHECK(!perr.empty());
        XER_VE_CHECK(text_id == -1);

        // Failed imports must not leave rows behind.
        XER_VE_CHECK(CountRows(cat.List(&perr)) == (wav_ok ? 1u : 0u));
    }

    // -- 8. UTF-8 (Cyrillic + emoji) media path ----------------------------
    {
        // "медиа 🚬.mp4" as raw UTF-8 bytes — hex escapes keep the test
        // independent of the compiler's source charset (same approach as
        // the store suite's kUtf8Source).
        const auto unicode_path = Path::FromUtf8(Utf8Path(dir)) /
            Path::FromUtf8(
                "\xD0\xBC\xD0\xB5\xD0\xB4\xD0\xB8\xD0\xB0 "
                "\xF0\x9F\x9A\xAC.mp4");
        std::error_code copy_ec;
        std::filesystem::copy_file(video_path, unicode_path,
                                   std::filesystem::copy_options::overwrite_existing,
                                   copy_ec);
        XER_VE_CHECK(!copy_ec);
        const std::string unicode_utf8 = Utf8Path(unicode_path);

        std::int64_t uni_id = 0;
        perr.clear();
        XER_VE_CHECK(cat.Import(unicode_utf8, &uni_id, false, &perr));
        XER_VE_CHECK(uni_id > 0);

        CatalogEntry uni;
        XER_VE_CHECK(cat.FindByPath(unicode_utf8, &uni, &perr));
        XER_VE_CHECK(uni.id == uni_id);
        // Stored path round-trips byte-exact through SQLite TEXT.
        const std::string expected = Utf8Path(
            std::filesystem::weakly_canonical(unicode_path));
        XER_VE_CHECK(uni.path == expected);
        // The Cyrillic stem survives as UTF-8 bytes.
        XER_VE_CHECK(uni.path.find("\xD0\xBC\xD0\xB5\xD0\xB4\xD0\xB8\xD0\xB0") !=
                     std::string::npos);
        XER_VE_CHECK(uni.width == kVideoW);
        // extract_poster=false -> no poster written for this row.
        XER_VE_CHECK(uni.poster_path.empty());
    }

    return 0;
}
