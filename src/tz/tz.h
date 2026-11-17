// tz.h — XER TZ archive format.
//
// TZ is a tiny indexed container designed for the .xexrj extension
// package: all member files live in one blob with a trailing index —
// entries can be read in place without extraction (the extension host
// runs main.js straight out of the archive, and the xer-ext:// CEF
// scheme serves static assets from memory).
//
// Layout (all integers little-endian):
//   [8]   magic "XERTZ001"
//   [u32] index_offset   — absolute offset of the index block
//   [u32] index_count
//   [u64] payload_size   — bytes between header and index
//   payload: concatenated file contents (raw, no compression)
//   index: per entry —
//     [u16 name_len][name bytes][u64 offset][u64 size][u32 crc32]
//     (offset/size are relative to the payload start)
//   OVERFLOW: generated member named "OVERFLOW" — one line per entry:
//     "<crc32-hex> <size> <name>" sorted by name. A packer regenerates
//     it unconditionally; a verifier cross-checks it against the index
//     so single-file tampering inside a re-indexed archive is caught.
//
// Limits: <= 65535 entries, names <= 1024 bytes (utf-8, '/'-separated,
// no absolute paths, no ".."), single file <= 4 GB.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace XER::Tz {

struct TzEntry {
    std::string name;     // utf-8, forward slashes, normalized
    std::uint64_t offset = 0;   // relative to payload start
    std::uint64_t size = 0;
    std::uint32_t crc32 = 0;
};

struct TzVerifyReport {
    bool ok = false;
    std::vector<std::string> errors;    // empty when ok
    std::vector<std::string> warnings;  // non-fatal notes
    std::size_t entries = 0;
    std::uint64_t total_bytes = 0;
};

// ---- reading ------------------------------------------------------------

// An opened archive; keeps the whole blob in memory (extensions are
// small — a few MB). Entries() is index-order.
class TzArchive {
public:
    // Parses a TZ blob held in memory. nullptr on a malformed header
    // (error receives a reason).
    static std::unique_ptr<TzArchive> FromMemory(
        std::vector<std::uint8_t> bytes, std::string* error);
    // Reads the whole file then FromMemory.
    static std::unique_ptr<TzArchive> Open(
        const std::string& path, std::string* error);

    const std::vector<TzEntry>& Entries() const { return entries_; }
    bool Has(const std::string& name) const;
    const TzEntry* Find(const std::string& name) const;
    // Returns a copy of the member's bytes; false when absent.
    bool Read(const std::string& name, std::string* out) const;
    bool Read(const std::string& name,
              std::vector<std::uint8_t>* out) const;
    // Pointer view into the member — no copy; valid while the archive
    // object lives. nullptr when absent.
    const std::uint8_t* Data(const std::string& name,
                             std::size_t* size) const;
    std::vector<std::string> Names() const;

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<TzEntry> entries_;
    std::map<std::string, std::size_t> by_name_;
    std::uint64_t payload_base_ = 0;
};

// ---- writing ------------------------------------------------------------

// Packs every file under `dir` (recursive, '/'-separated relative
// names) into a TZ at `out_path`. A fresh OVERFLOW member is generated
// and replaces any existing one in the source dir. Files are ordered
// by name for deterministic output. error receives a message on
// failure (unreadable file, oversize member, unwritable output).
bool TzWriteDir(const std::string& dir, const std::string& out_path,
                std::string* error);

// Same but from an explicit (name -> bytes) map — used by tests and by
// callers synthesizing members. OVERFLOW is regenerated the same way.
bool TzWrite(const std::map<std::string, std::vector<std::uint8_t>>&
                 files,
             const std::string& out_path, std::string* error);

// ---- verification -------------------------------------------------------

// Full integrity pass on a TZ file: magic/index bounds, every member's
// stored crc32 vs computed, and the OVERFLOW manifest vs the index
// (missing/extra/mismatched entries). Also verifies `requires` — the
// member names a manifest.toml declared (the caller merges them in).
TzVerifyReport TzVerify(
    const std::string& path,
    const std::vector<std::string>& required = {});

// Convenience text for CLI tools: one summary line per problem.
std::string TzReportText(const TzVerifyReport& r);

std::uint32_t TzCrc32(const void* data, std::size_t size);
// Validates a member name; false on absolute paths, ".." or drive
// letters, backslashes converted to '/' first.
bool TzIsSafeName(const std::string& name);
// Normalizes separators + strips leading "./". "" on unsafe names.
std::string TzNormalizeName(const std::string& name);

}  // namespace XER::Tz
