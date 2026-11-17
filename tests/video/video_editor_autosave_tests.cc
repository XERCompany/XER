// Autosave + PosterCache suite — Premiere-style rotating autosaves
// (dirty gating, interval gating, slot rotation with the oldest
// dropped, ListAutosaves newest-first ordering, LoadLatest round-trip
// and corrupt-newest fallback via ProjectStore) plus the thread-safe
// byte-budgeted LRU poster cache (hit/miss, eviction order, capacity
// accounting, bucket quantization). Pure stdlib + TempDir fixtures —
// no FFmpeg, no skips.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "frame.h"
#include "helper/path_utils.h"
#include "poster_cache.h"
#include "project_autosave.h"
#include "project_store.h"
#include "timeline_model.h"

namespace {
using namespace XER::VideoEditor;
namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

bool Exists(const std::string& utf8_path) {
    return std::filesystem::exists(Path::FromUtf8(utf8_path));
}

bool EndsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(),
                        suffix) == 0;
}

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

// Minimal valid project — one video track + one clip; `width` tags
// each revision so round-trips identify which autosave slot loaded.
Project MakeProject(int width) {
    Project p;
    p.width = width;
    p.height = 720;
    p.fps_num = 24;
    p.fps_den = 1;
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
    Clip c;
    c.id = "clip-1";
    c.source_path = "D:/media/a.mp4";
    c.track = 0;
    c.timeline_start = 0.0;
    c.in_point = 0.0;
    c.duration = 5.0;
    p.clips.push_back(c);
    return p;
}

// w x h BGRA8 frame filled with `value` — DataSize() == w*h*4 is what
// the cache accounts.
video::Frame MakeFrame(int w, int h, std::uint8_t value) {
    video::Frame f(w, h, video::PixelFormat::BGRA8);
    for (std::size_t i = 0; i < f.DataSize(); ++i) {
        f.Data()[i] = value;
    }
    return f;
}

void RunAutosaveCases(const std::filesystem::path& base) {
    const std::string proj = Utf8Path(base / "movie.xerproj");
    const std::string adir = Utf8Path(base / "autosaves");
    std::string err;
    const Project project = MakeProject(1280);

    // -- 1. Init validation + calls before Init ------------------------
    {
        AutosaveManager mgr;
        XER_VE_CHECK(!mgr.IsInitialized());
        XER_VE_CHECK(!mgr.MaybeAutosave(project, 0, &err));
        XER_VE_CHECK(!err.empty());
        err.clear();
        XER_VE_CHECK(!mgr.Init("", adir, &err));
        XER_VE_CHECK(!err.empty());
        err.clear();
        XER_VE_CHECK(!mgr.Init(proj, "", &err));
        XER_VE_CHECK(!err.empty());
        err.clear();
        XER_VE_CHECK(mgr.Init(proj, adir, &err));
        XER_VE_CHECK(mgr.IsInitialized());
        XER_VE_CHECK(
            std::filesystem::is_directory(Path::FromUtf8(adir)));
        // No autosaves yet: empty list, LoadLatest fails cleanly.
        XER_VE_CHECK(mgr.ListAutosaves().empty());
        Project out;
        err.clear();
        XER_VE_CHECK(!mgr.LoadLatest(&out, &err));
        XER_VE_CHECK(!err.empty());
        err.clear();
        XER_VE_CHECK(!AutosaveManager::LoadLatest(proj, adir, &out, &err));
        XER_VE_CHECK(!err.empty());
    }

    // -- 2. dirty gating + first write ---------------------------------
    {
        const std::string dir2 = Utf8Path(base / "autosaves_dirty");
        AutosaveManager mgr;
        XER_VE_CHECK(mgr.Init(proj, dir2, &err));
        XER_VE_CHECK(!mgr.IsDirty());

        // Not dirty -> success no-op, nothing written.
        err.clear();
        XER_VE_CHECK(mgr.MaybeAutosave(project, 0, &err));
        XER_VE_CHECK(!Exists(
            AutosaveManager::AutosavePathFor(proj, dir2, 1)));

        mgr.MarkDirty();
        XER_VE_CHECK(mgr.IsDirty());
        XER_VE_CHECK(mgr.MaybeAutosave(project, 0, &err));
        XER_VE_CHECK(!mgr.IsDirty());
        XER_VE_CHECK(Exists(
            AutosaveManager::AutosavePathFor(proj, dir2, 1)));
        XER_VE_CHECK(!Exists(
            AutosaveManager::AutosavePathFor(proj, dir2, 2)));
        // Saved content is a real .xerproj.
        Project out;
        XER_VE_CHECK(ProjectStore::Load(
            AutosaveManager::AutosavePathFor(proj, dir2, 1), &out, &err));
        XER_VE_CHECK(out.width == 1280 && out.clips.size() == 1);
    }

    // -- 3. interval gating ---------------------------------------------
    {
        const std::string dir3 = Utf8Path(base / "autosaves_interval");
        AutosaveManager mgr;
        XER_VE_CHECK(mgr.Init(proj, dir3, &err));
        mgr.SetIntervalMs(10000);
        XER_VE_CHECK(mgr.IntervalMs() == 10000);

        mgr.MarkDirty();
        XER_VE_CHECK(mgr.MaybeAutosave(project, 0, &err));   // first: due
        XER_VE_CHECK(Exists(
            AutosaveManager::AutosavePathFor(proj, dir3, 1)));

        // Dirty again but inside the interval -> no-op, no rotation.
        mgr.MarkDirty();
        XER_VE_CHECK(mgr.MaybeAutosave(project, 5000, &err));
        XER_VE_CHECK(mgr.IsDirty());   // gated, still dirty
        XER_VE_CHECK(!Exists(
            AutosaveManager::AutosavePathFor(proj, dir3, 2)));
        XER_VE_CHECK(mgr.MaybeAutosave(project, 9999, &err));
        XER_VE_CHECK(!Exists(
            AutosaveManager::AutosavePathFor(proj, dir3, 2)));

        // Exactly at the interval boundary -> due: rotate + write.
        XER_VE_CHECK(mgr.MaybeAutosave(project, 10000, &err));
        XER_VE_CHECK(Exists(
            AutosaveManager::AutosavePathFor(proj, dir3, 1)));
        XER_VE_CHECK(Exists(
            AutosaveManager::AutosavePathFor(proj, dir3, 2)));
    }

    // -- 4. rotation: 7 saves -> slots 1..5, oldest dropped -------------
    {
        const std::string dir4 = Utf8Path(base / "autosaves_rot");
        AutosaveManager mgr;
        XER_VE_CHECK(mgr.Init(proj, dir4, &err));
        mgr.SetIntervalMs(0);   // every dirty call is due

        for (int i = 1; i <= 7; ++i) {
            const Project rev = MakeProject(1000 + i);
            mgr.MarkDirty();
            XER_VE_CHECK(
                mgr.MaybeAutosave(rev, static_cast<std::int64_t>(i) * 1000,
                                  &err));
        }
        for (int n = 1; n <= AutosaveManager::kMaxAutosaves; ++n) {
            XER_VE_CHECK(
                Exists(AutosaveManager::AutosavePathFor(proj, dir4, n)));
        }
        XER_VE_CHECK(!Exists(AutosaveManager::AutosavePathFor(
            proj, dir4, AutosaveManager::kMaxAutosaves + 1)));

        // Stray files that don't match the slot pattern are ignored.
        XER_VE_CHECK(WriteTextFile(dir4 + "/movie.autosave-x.xerproj",
                                   "junk"));
        XER_VE_CHECK(
            WriteTextFile(dir4 + "/other.autosave-1.xerproj", "junk"));
        XER_VE_CHECK(WriteTextFile(dir4 + "/movie.autosave-9.txt", "junk"));

        // ListAutosaves: newest-first (ascending slot N).
        const std::vector<std::string> list = mgr.ListAutosaves();
        XER_VE_CHECK(list.size() ==
                     static_cast<std::size_t>(AutosaveManager::kMaxAutosaves));
        for (std::size_t i = 0; i < list.size(); ++i) {
            XER_VE_CHECK(EndsWith(list[i], ".autosave-" +
                                               std::to_string(i + 1) +
                                               ".xerproj"));
        }
        // Static form agrees with the bound instance.
        XER_VE_CHECK(AutosaveManager::ListAutosaves(proj, dir4).size() ==
                     list.size());

        // Slot 1 holds the newest revision (save #7, width 1007);
        // slot 5 the oldest surviving one (save #3, width 1003) —
        // saves #1/#2 rotated out entirely.
        if (list.size() == 5) {
            Project out;
            XER_VE_CHECK(ProjectStore::Load(list.front(), &out, &err));
            XER_VE_CHECK(out.width == 1007);
            XER_VE_CHECK(ProjectStore::Load(list.back(), &out, &err));
            XER_VE_CHECK(out.width == 1003);
        }
    }

    // -- 5. LoadLatest: round-trip + corrupt-newest fallback ------------
    {
        const std::string dir5 = Utf8Path(base / "autosaves_rec");
        AutosaveManager mgr;
        XER_VE_CHECK(mgr.Init(proj, dir5, &err));
        mgr.SetIntervalMs(0);

        mgr.MarkDirty();
        XER_VE_CHECK(mgr.MaybeAutosave(MakeProject(1280), 0, &err));
        Project newer = MakeProject(1920);
        Clip extra;
        extra.id = "clip-2";
        extra.source_path = "D:/media/b.mp4";
        extra.track = 0;
        extra.timeline_start = 5.0;
        extra.in_point = 0.0;
        extra.duration = 4.0;
        newer.clips.push_back(extra);
        mgr.MarkDirty();
        XER_VE_CHECK(mgr.MaybeAutosave(newer, 1, &err));

        Project out;
        XER_VE_CHECK(mgr.LoadLatest(&out, &err));
        XER_VE_CHECK(out.width == 1920);
        XER_VE_CHECK(out.clips.size() == 2);
        // Static form reaches the same newest slot.
        Project out2;
        XER_VE_CHECK(AutosaveManager::LoadLatest(proj, dir5, &out2, &err));
        XER_VE_CHECK(out2.width == 1920);

        // Corrupt the newest slot -> recovery falls back to the older
        // copy instead of failing.
        XER_VE_CHECK(WriteTextFile(
            AutosaveManager::AutosavePathFor(proj, dir5, 1),
            "this is { not json ]]]"));
        Project recovered;
        XER_VE_CHECK(mgr.LoadLatest(&recovered, &err));
        XER_VE_CHECK(recovered.width == 1280);
        XER_VE_CHECK(recovered.clips.size() == 1);
    }
}

void RunPosterCacheCases() {
    // -- 1. miss -> put -> hit; key parts are independent --------------
    {
        PosterCache cache;
        const PosterCache::Stats st0 = cache.GetStats();
        XER_VE_CHECK(st0.entries == 0 && st0.bytes == 0 &&
                     st0.hits == 0 && st0.misses == 0);

        XER_VE_CHECK(cache.Get("a.mp4", 0) == nullptr);
        XER_VE_CHECK(cache.GetStats().misses == 1);

        cache.Put("a.mp4", 0, MakeFrame(8, 8, 7));
        const std::shared_ptr<const video::Frame> hit =
            cache.Get("a.mp4", 0);
        XER_VE_CHECK(hit != nullptr);
        if (hit != nullptr) {
            XER_VE_CHECK(hit->IsValid());
            XER_VE_CHECK(hit->Width() == 8 && hit->Height() == 8);
            XER_VE_CHECK(hit->Format() == video::PixelFormat::BGRA8);
            XER_VE_CHECK(hit->DataSize() == 8u * 8u * 4u);
            XER_VE_CHECK(hit->Data()[0] == 7);
        }
        const PosterCache::Stats st = cache.GetStats();
        XER_VE_CHECK(st.hits == 1 && st.misses == 1);
        XER_VE_CHECK(st.entries == 1 && st.bytes == 8u * 8u * 4u);
        XER_VE_CHECK(st.capacity_bytes ==
                     PosterCache::kDefaultCapacityBytes);

        // Different bucket and different path are distinct keys.
        XER_VE_CHECK(cache.Get("a.mp4", 500) == nullptr);
        XER_VE_CHECK(cache.Get("b.mp4", 0) == nullptr);
        XER_VE_CHECK(cache.GetStats().misses == 3);
    }

    // -- 2. LRU eviction + capacity accounting --------------------------
    {
        constexpr std::size_t kFrameBytes = 10u * 10u * 4u;   // 400
        PosterCache cache(kFrameBytes * 3);
        cache.Put("m", 0, MakeFrame(10, 10, 1));   // A
        cache.Put("m", 1, MakeFrame(10, 10, 2));   // B
        cache.Put("m", 2, MakeFrame(10, 10, 3));   // C -- full
        PosterCache::Stats st = cache.GetStats();
        XER_VE_CHECK(st.entries == 3 && st.bytes == kFrameBytes * 3);

        // Touch A -> B is now the LRU tail; inserting D evicts B.
        XER_VE_CHECK(cache.Get("m", 0) != nullptr);
        cache.Put("m", 3, MakeFrame(10, 10, 4));   // D
        XER_VE_CHECK(cache.Get("m", 1) == nullptr);   // B evicted
        XER_VE_CHECK(cache.Get("m", 0) != nullptr);   // A kept
        XER_VE_CHECK(cache.Get("m", 2) != nullptr);   // C kept
        XER_VE_CHECK(cache.Get("m", 3) != nullptr);   // D kept
        st = cache.GetStats();
        XER_VE_CHECK(st.evictions == 1);
        XER_VE_CHECK(st.entries == 3 && st.bytes == kFrameBytes * 3);

        // Re-put an existing key: replaces, keeps single entry.
        cache.Put("m", 3, MakeFrame(4, 4, 9));
        st = cache.GetStats();
        XER_VE_CHECK(st.entries == 3);
        XER_VE_CHECK(st.bytes == kFrameBytes * 2 + 4u * 4u * 4u);
        const std::shared_ptr<const video::Frame> d = cache.Get("m", 3);
        XER_VE_CHECK(d != nullptr);
        if (d != nullptr) {
            XER_VE_CHECK(d->Width() == 4 && d->Data()[0] == 9);
        }
    }

    // -- 3. SetCapacity shrinks evict; oversized frames never cached ----
    {
        constexpr std::size_t kFrameBytes = 10u * 10u * 4u;
        PosterCache cache(kFrameBytes * 3);
        cache.Put("m", 0, MakeFrame(10, 10, 1));
        cache.Put("m", 1, MakeFrame(10, 10, 2));
        cache.Put("m", 2, MakeFrame(10, 10, 3));
        // MRU order: 2,1,0. Shrink to one frame -> evicts 0 then 1.
        cache.SetCapacityBytes(kFrameBytes);
        PosterCache::Stats st = cache.GetStats();
        XER_VE_CHECK(st.entries == 1 && st.bytes == kFrameBytes);
        XER_VE_CHECK(st.evictions == 2);
        XER_VE_CHECK(cache.Get("m", 2) != nullptr);
        XER_VE_CHECK(cache.Get("m", 0) == nullptr);
        XER_VE_CHECK(cache.Get("m", 1) == nullptr);

        // Capacity 0 empties everything.
        cache.SetCapacityBytes(0);
        st = cache.GetStats();
        XER_VE_CHECK(st.entries == 0 && st.bytes == 0);
        XER_VE_CHECK(cache.Get("m", 2) == nullptr);

        // A single frame larger than the budget is never retained.
        PosterCache tiny(100);
        tiny.Put("big", 0, MakeFrame(10, 10, 5));   // 400 > 100
        XER_VE_CHECK(tiny.Get("big", 0) == nullptr);
        XER_VE_CHECK(tiny.GetStats().entries == 0);
        XER_VE_CHECK(tiny.GetStats().bytes == 0);
    }

    // -- 4. Remove / RemoveMedia / Clear --------------------------------
    {
        PosterCache cache;
        cache.Put("x", 0, MakeFrame(4, 4, 1));
        cache.Put("x", 1, MakeFrame(4, 4, 2));
        cache.Put("y", 0, MakeFrame(4, 4, 3));

        cache.RemoveMedia("x");
        XER_VE_CHECK(cache.Get("x", 0) == nullptr);
        XER_VE_CHECK(cache.Get("x", 1) == nullptr);
        XER_VE_CHECK(cache.Get("y", 0) != nullptr);
        XER_VE_CHECK(cache.GetStats().entries == 1);

        cache.Remove("y", 0);
        XER_VE_CHECK(cache.Get("y", 0) == nullptr);
        XER_VE_CHECK(cache.GetStats().entries == 0);

        // Removing a missing key is a no-op; Clear empties the cache.
        cache.Remove("y", 0);
        cache.Put("z", 0, MakeFrame(4, 4, 9));
        cache.Clear();
        const PosterCache::Stats st = cache.GetStats();
        XER_VE_CHECK(st.entries == 0 && st.bytes == 0);
        XER_VE_CHECK(cache.Get("z", 0) == nullptr);
    }

    // -- 5. BucketFor quantization ---------------------------------------
    {
        XER_VE_CHECK(PosterCache::BucketFor(1.2, 0.5) == 1000);
        XER_VE_CHECK(PosterCache::BucketFor(1.3, 0.5) == 1500);
        XER_VE_CHECK(PosterCache::BucketFor(0.0, 0.5) == 0);
        XER_VE_CHECK(PosterCache::BucketFor(-0.4, 0.5) == -500);
        XER_VE_CHECK(PosterCache::BucketFor(2.0) == 2000);   // default 0.5
        // Non-positive bucket -> raw milliseconds, no sharing.
        XER_VE_CHECK(PosterCache::BucketFor(1.5, 0.0) == 1500);
        XER_VE_CHECK(PosterCache::BucketFor(1.5, -1.0) == 1500);
    }
}

}  // namespace

int XER_VideoEditor_RunAutosaveTests() {
    const auto base = XER::VideoEditor::Test::TempDir() / "autosave";
    // TempDir is stable across runs — wipe slot files left by a
    // previous pass so the !Exists() checks stay deterministic.
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base);
    RunAutosaveCases(base);
    RunPosterCacheCases();
    return 0;
}
