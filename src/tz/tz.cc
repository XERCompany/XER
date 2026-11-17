// tz.cc — see tz.h for the format spec.
#include "tz.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace XER::Tz {
namespace {

constexpr char kMagic[8] = {'X', 'E', 'R', 'T', 'Z', '0', '0', '1'};
constexpr std::size_t kHeaderSize = 8 + 4 + 4 + 8;  // 24
constexpr std::size_t kMaxEntries = 65535;
constexpr std::size_t kMaxNameLen = 1024;
constexpr std::uint64_t kMaxMemberSize = 4ull * 1024 * 1024 * 1024;
constexpr const char* kOverflowName = "OVERFLOW";

// Little-endian writers.
void PutU16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void PutU32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<std::uint8_t>(x >> (i * 8)));
}
void PutU64(std::vector<std::uint8_t>& v, std::uint64_t x) {
    for (int i = 0; i < 8; ++i)
        v.push_back(static_cast<std::uint8_t>(x >> (i * 8)));
}

// Little-endian readers over a span — all bounds-checked.
struct Cursor {
    const std::uint8_t* p;
    const std::uint8_t* end;
    bool U16(std::uint16_t* x) {
        if (end - p < 2) return false;
        *x = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
        p += 2;
        return true;
    }
    bool U32(std::uint32_t* x) {
        if (end - p < 4) return false;
        *x = static_cast<std::uint32_t>(p[0]) |
             (static_cast<std::uint32_t>(p[1]) << 8) |
             (static_cast<std::uint32_t>(p[2]) << 16) |
             (static_cast<std::uint32_t>(p[3]) << 24);
        p += 4;
        return true;
    }
    bool U64(std::uint64_t* x) {
        if (end - p < 8) return false;
        *x = 0;
        for (int i = 0; i < 8; ++i)
            *x |= static_cast<std::uint64_t>(p[i]) << (i * 8);
        p += 8;
        return true;
    }
};

// The canonical OVERFLOW text — "<crc32-8hex> <size> <name>\n".
std::string BuildOverflow(const std::vector<TzEntry>& entries) {
    std::ostringstream out;
    char hex[16];
    for (const auto& e : entries) {
        std::snprintf(hex, sizeof hex, "%08x", e.crc32);
        out << hex << ' ' << e.size << ' ' << e.name << '\n';
    }
    return out.str();
}

bool LoadFile(const std::string& path, std::vector<std::uint8_t>* out,
              std::string* error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    const auto size = f.tellg();
    if (size < 0 ||
        static_cast<std::uint64_t>(size) > kMaxMemberSize) {
        if (error) *error = "file too large: " + path;
        return false;
    }
    f.seekg(0);
    out->resize(static_cast<std::size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(out->data()), size);
    }
    if (!f && size > 0) {
        if (error) *error = "read failed: " + path;
        return false;
    }
    return true;
}

}  // namespace

// ---- crc32 (IEEE, table built once) -------------------------------------

std::uint32_t TzCrc32(const void* data, std::size_t size) {
    static std::uint32_t table[256];
    static bool built = [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        return true;
    }();
    (void)built;
    std::uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---- names ---------------------------------------------------------------

bool TzIsSafeName(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameLen) return false;
    if (name.front() == '/' || name.front() == '\\') return false;
    if (name.size() > 1 && name[1] == ':') return false;   // drive letter
    std::string norm;
    norm.reserve(name.size());
    int depth = 0;
    bool seg_start = true;
    std::string seg;
    for (const char c : name) {
        if (c == '\0') return false;
        if (c == '/' || c == '\\') {
            if (seg == "..") return false;
            if (!seg.empty() && seg != ".") ++depth;
            seg.clear();
            seg_start = true;
            continue;
        }
        seg.push_back(c);
        (void)seg_start;
    }
    if (seg == "..") return false;
    return depth >= 0;   // all ".." rejected inline above
}

std::string TzNormalizeName(const std::string& name) {
    std::string n = name;
    std::replace(n.begin(), n.end(), '\\', '/');
    while (n.rfind("./", 0) == 0) n.erase(0, 2);
    // Collapse duplicate slashes.
    std::string out;
    out.reserve(n.size());
    char prev = '\0';
    for (const char c : n) {
        if (c == '/' && prev == '/') continue;
        out.push_back(c);
        prev = c;
    }
    while (!out.empty() && out.front() == '/') out.erase(0, 1);
    while (!out.empty() && out.back() == '/') out.pop_back();
    return TzIsSafeName(out) ? out : std::string{};
}

// ---- reading -------------------------------------------------------------

std::unique_ptr<TzArchive> TzArchive::FromMemory(
    std::vector<std::uint8_t> bytes, std::string* error) {
    if (bytes.size() < kHeaderSize) {
        if (error) *error = "truncated TZ header";
        return nullptr;
    }
    if (std::memcmp(bytes.data(), kMagic, 8) != 0) {
        if (error) *error = "not a TZ archive (bad magic)";
        return nullptr;
    }
    Cursor cur{bytes.data() + 8, bytes.data() + bytes.size()};
    std::uint32_t index_off = 0, index_count = 0;
    std::uint64_t payload_size = 0;
    if (!cur.U32(&index_off) || !cur.U32(&index_count) ||
        !cur.U64(&payload_size)) {
        if (error) *error = "truncated TZ header fields";
        return nullptr;
    }
    if (index_count > kMaxEntries ||
        static_cast<std::uint64_t>(index_off) >= bytes.size() ||
        payload_size > index_off - kHeaderSize ||
        kHeaderSize + payload_size > index_off) {
        if (error) *error = "TZ index offset/count out of bounds";
        return nullptr;
    }

    auto arc = std::make_unique<TzArchive>();
    arc->bytes_ = std::move(bytes);
    arc->payload_base_ = kHeaderSize;
    arc->entries_.reserve(index_count);
    Cursor ic{arc->bytes_.data() + index_off,
              arc->bytes_.data() + arc->bytes_.size()};
    for (std::uint32_t i = 0; i < index_count; ++i) {
        std::uint16_t name_len = 0;
        TzEntry e;
        if (!ic.U16(&name_len) || name_len == 0 ||
            ic.end - ic.p < name_len + 8 + 8 + 4) {
            if (error) *error = "truncated TZ index entry";
            return nullptr;
        }
        e.name.assign(reinterpret_cast<const char*>(ic.p), name_len);
        ic.p += name_len;
        if (!ic.U64(&e.offset) || !ic.U64(&e.size) ||
            !ic.U32(&e.crc32)) {
            if (error) *error = "truncated TZ index fields";
            return nullptr;
        }
        if (e.offset + e.size > payload_size) {
            if (error) {
                *error = "member '" + e.name +
                         "' points outside payload";
            }
            return nullptr;
        }
        const std::string norm = TzNormalizeName(e.name);
        if (norm.empty()) {
            if (error) {
                *error = "unsafe member name: " + e.name;
            }
            return nullptr;
        }
        e.name = norm;
        arc->by_name_[e.name] = arc->entries_.size();
        arc->entries_.push_back(std::move(e));
    }
    return arc;
}

std::unique_ptr<TzArchive> TzArchive::Open(const std::string& path,
                                           std::string* error) {
    std::vector<std::uint8_t> bytes;
    if (!LoadFile(path, &bytes, error)) {
        return nullptr;
    }
    return FromMemory(std::move(bytes), error);
}

bool TzArchive::Has(const std::string& name) const {
    return by_name_.find(TzNormalizeName(name)) != by_name_.end();
}

const TzEntry* TzArchive::Find(const std::string& name) const {
    const auto it = by_name_.find(TzNormalizeName(name));
    return it == by_name_.end() ? nullptr : &entries_[it->second];
}

const std::uint8_t* TzArchive::Data(const std::string& name,
                                    std::size_t* size) const {
    const TzEntry* e = Find(name);
    if (e == nullptr) return nullptr;
    if (size != nullptr) *size = static_cast<std::size_t>(e->size);
    return bytes_.data() + payload_base_ + e->offset;
}

bool TzArchive::Read(const std::string& name,
                     std::vector<std::uint8_t>* out) const {
    std::size_t size = 0;
    const std::uint8_t* p = Data(name, &size);
    if (p == nullptr) return false;
    out->assign(p, p + size);
    return true;
}

bool TzArchive::Read(const std::string& name, std::string* out) const {
    std::size_t size = 0;
    const std::uint8_t* p = Data(name, &size);
    if (p == nullptr) return false;
    out->assign(reinterpret_cast<const char*>(p), size);
    return true;
}

std::vector<std::string> TzArchive::Names() const {
    std::vector<std::string> v;
    v.reserve(entries_.size());
    for (const auto& e : entries_) v.push_back(e.name);
    return v;
}

// ---- writing -------------------------------------------------------------

bool TzWrite(const std::map<std::string, std::vector<std::uint8_t>>&
                 files,
             const std::string& out_path, std::string* error) {
    if (files.size() > kMaxEntries) {
        if (error) *error = "too many entries (max 65535)";
        return false;
    }
    std::vector<TzEntry> entries;
    std::vector<std::uint8_t> payload;
    entries.reserve(files.size());
    for (const auto& [raw_name, data] : files) {
        const std::string name = TzNormalizeName(raw_name);
        if (name.empty()) {
            if (error) *error = "unsafe member name: " + raw_name;
            return false;
        }
        if (name == kOverflowName) {
            continue;   // regenerated below
        }
        if (data.size() > kMaxMemberSize) {
            if (error) *error = "member too large: " + name;
            return false;
        }
        TzEntry e;
        e.name = name;
        e.offset = payload.size();
        e.size = data.size();
        e.crc32 = TzCrc32(data.data(), data.size());
        payload.insert(payload.end(), data.begin(), data.end());
        entries.push_back(std::move(e));
    }
    // OVERFLOW covers all real members; its own entry is appended last
    // so the manifest itself is verifiable by index crc as well.
    std::string overflow = BuildOverflow(entries);
    {
        TzEntry e;
        e.name = kOverflowName;
        e.offset = payload.size();
        e.size = overflow.size();
        e.crc32 = TzCrc32(overflow.data(), overflow.size());
        payload.insert(payload.end(), overflow.begin(), overflow.end());
        entries.push_back(std::move(e));
    }

    std::vector<std::uint8_t> blob;
    blob.reserve(kHeaderSize + payload.size() +
                 entries.size() * 32);
    blob.insert(blob.end(), kMagic, kMagic + 8);
    const std::uint64_t index_off = kHeaderSize + payload.size();
    PutU32(blob, static_cast<std::uint32_t>(index_off));
    PutU32(blob, static_cast<std::uint32_t>(entries.size()));
    PutU64(blob, payload.size());
    blob.insert(blob.end(), payload.begin(), payload.end());
    for (const auto& e : entries) {
        PutU16(blob, static_cast<std::uint16_t>(e.name.size()));
        blob.insert(blob.end(), e.name.begin(), e.name.end());
        PutU64(blob, e.offset);
        PutU64(blob, e.size);
        PutU32(blob, e.crc32);
    }

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + out_path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(blob.data()),
              static_cast<std::streamsize>(blob.size()));
    if (!out) {
        if (error) *error = "write failed: " + out_path;
        return false;
    }
    return true;
}

bool TzWriteDir(const std::string& dir, const std::string& out_path,
                std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        if (error) *error = "not a directory: " + dir;
        return false;
    }
    std::map<std::string, std::vector<std::uint8_t>> files;
    for (const auto& de :
         fs::recursive_directory_iterator(dir, ec)) {
        std::error_code ec2;
        if (!de.is_regular_file(ec2)) {
            continue;
        }
        const std::string rel =
            TzNormalizeName(
                fs::relative(de.path(), dir, ec).generic_string());
        if (rel.empty()) {
            if (error) {
                *error = "cannot map to a safe member name: " +
                        de.path().string();
            }
            return false;
        }
        std::vector<std::uint8_t> bytes;
        if (!LoadFile(de.path().string(), &bytes, error)) {
            return false;
        }
        files[rel] = std::move(bytes);
    }
    if (ec) {
        if (error) *error = "directory walk failed: " + ec.message();
        return false;
    }
    return TzWrite(files, out_path, error);
}

// ---- verification --------------------------------------------------------

TzVerifyReport TzVerify(const std::string& path,
                        const std::vector<std::string>& required) {
    TzVerifyReport r;
    auto fail = [&](const std::string& e) {
        r.errors.push_back(e);
    };

    std::string err;
    auto arc = TzArchive::Open(path, &err);
    if (!arc) {
        fail(err);
        return r;
    }
    r.entries = arc->Entries().size();
    for (const auto& e : arc->Entries()) {
        r.total_bytes += e.size;
    }

    // Per-member crc vs index.
    for (const auto& e : arc->Entries()) {
        std::size_t size = 0;
        const std::uint8_t* p = arc->Data(e.name, &size);
        if (p == nullptr) {
            fail("member vanished after open: " + e.name);
            continue;
        }
        const std::uint32_t actual = TzCrc32(p, size);
        if (actual != e.crc32) {
            char hex[16];
            std::snprintf(hex, sizeof hex, "%08x", actual);
            fail("crc mismatch " + e.name + " (index " +
                 std::to_string(e.crc32) + " != computed " + hex + ")");
        }
    }

    // OVERFLOW cross-check.
    std::string overflow;
    if (!arc->Read(kOverflowName, &overflow)) {
        fail("OVERFLOW manifest missing");
    } else {
        std::map<std::string, std::pair<std::uint32_t, std::uint64_t>>
            listed;
        std::istringstream in(overflow);
        std::string line;
        std::size_t lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            if (line.empty()) continue;
            std::istringstream ls(line);
            std::string hex, name;
            std::uint64_t size = 0;
            if (!(ls >> hex >> size >> name) ||
                hex.size() != 8) {
                fail("OVERFLOW line " + std::to_string(lineno) +
                     " malformed");
                continue;
            }
            listed[name] = {static_cast<std::uint32_t>(
                                std::stoul(hex, nullptr, 16)),
                            size};
        }
        for (const auto& e : arc->Entries()) {
            if (e.name == kOverflowName) continue;
            const auto it = listed.find(e.name);
            if (it == listed.end()) {
                fail("OVERFLOW does not list " + e.name);
            } else if (it->second.first != e.crc32 ||
                       it->second.second != e.size) {
                fail("OVERFLOW checksum/size mismatch for " + e.name);
            }
        }
        for (const auto& [name, _] : listed) {
            if (!arc->Has(name)) {
                fail("OVERFLOW lists missing member " + name);
            }
        }
    }

    // Manifest-declared requirements.
    for (const auto& req : required) {
        if (!arc->Has(req)) {
            fail("required member missing: " + req);
        }
    }

    r.ok = r.errors.empty();
    return r;
}

std::string TzReportText(const TzVerifyReport& r) {
    std::ostringstream out;
    if (r.ok) {
        out << "OK: " << r.entries << " entries, " << r.total_bytes
            << " bytes";
    }
    for (const auto& e : r.errors) out << "ERROR: " << e << '\n';
    for (const auto& w : r.warnings) out << "warn: " << w << '\n';
    return out.str();
}

}  // namespace XER::Tz
