#ifndef XER_UI_RENDERING_RENDER_STORE_H
#define XER_UI_RENDERING_RENDER_STORE_H

#include <cstdint>
#include <memory>
#include <string>

namespace XER::UI::Rendering {

// Named shared-memory blob store shared between the UI process
// (writer) and render workers (readers). It carries pixel data too
// large for the command channel — decoded images, live video frames
// and audio-analysis levels — so stream ops only reference a small
// resource id and workers read bytes directly.
//
//   Host   : RenderStore::Create("xer.ui.res.<pid>", capacity)
//   Worker : RenderStore::Open(same name)
//
// Layout: fixed header + slot table + bump arena. Slots are indexed
// by resource id (0..kMaxResources-1); each payload sits at a
// byte offset inside the arena. Writes are seqlocked: the writer
// marks seq odd before memcpy and even after, readers retry while
// the sequence is odd — video frames race a repaint without locks.
class RenderStore {
public:
    enum class Kind : std::uint32_t {
        kInvalid = 0,
        kImageBgra,    // static decoded bitmap, uploaded once
        kVideoFrame,   // BGRA frame buffer, rewritten per frame
        kSpectrumF32,  // float32 levels (audio analysis)
    };

    static constexpr std::uint32_t kMaxResources = 256;

    struct ResourceInfo {
        Kind kind = Kind::kInvalid;
        std::uint32_t width = 0;
        std::uint32_t height = 0;   // rows / element count
        std::uint32_t payload = 0;  // bytes
        std::uint32_t seq = 0;      // odd = being written
    };

    struct View {
        const void* data = nullptr;
        ResourceInfo info{};
    };

    ~RenderStore();
    RenderStore(const RenderStore&) = delete;
    RenderStore& operator=(const RenderStore&) = delete;

    static std::unique_ptr<RenderStore> Create(
        const std::string& name, std::uint32_t capacity_bytes,
        std::string* error_out = nullptr);
    static std::unique_ptr<RenderStore> Open(
        const std::string& name, std::string* error_out = nullptr);

    // Writer side -------------------------------------------------
    // Reserves (or reuses) the slot |id| for a |kind| payload of
    // |width| x |height| elements; returns the writable payload
    // pointer (nullptr on full store / bad id). Call FinishWrite
    // after filling — it flips the seqlock to even.
    void* BeginWrite(std::uint32_t id, Kind kind, std::uint32_t width,
                     std::uint32_t height, std::uint32_t payload);
    void FinishWrite(std::uint32_t id);
    void Free(std::uint32_t id);
    // One-shot helper for static payloads (images, spectrum).
    bool Write(std::uint32_t id, Kind kind, std::uint32_t width,
               std::uint32_t height, const void* data,
               std::uint32_t bytes);

    // Reader side -------------------------------------------------
    // Snapshot-reads the slot: retries while seq is odd, then returns
    // the payload pointer + metadata. data is nullptr for missing or
    // in-progress writes.
    View Read(std::uint32_t id) const;
    std::uint32_t Seq(std::uint32_t id) const;

    const std::string& name() const { return name_; }
    bool is_writer() const { return writer_; }

private:
    struct Entry {
        std::uint32_t kind = 0;
        std::uint32_t seq = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t payload = 0;
        std::uint32_t offset = 0;
        std::uint32_t reserved0 = 0;
        std::uint32_t reserved1 = 0;
    };
    struct Header {
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::uint32_t capacity = 0;
        std::uint32_t alloc_top = 0;
        Entry entries[kMaxResources];
    };

    RenderStore() = default;
    Entry* Slot(std::uint32_t id);
    const Entry* Slot(std::uint32_t id) const;

    std::string name_;
    bool writer_ = false;
    void* mapping_ = nullptr;   // HANDLE
    Header* shared_ = nullptr;
};

}  // namespace XER::UI::Rendering

#endif  // XER_UI_RENDERING_RENDER_STORE_H
