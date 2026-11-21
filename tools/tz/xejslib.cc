// xejslib.cc — see xejslib.h for the XJSL/XJSH layouts and the
// library-consumption contract. Clang-free: this file only packs,
// parses and extracts — CompileToXsjo (xsjo_compile.cc) owns the
// compile step, ParseXsjo owns payload validation on read.
#include "xejslib.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include "tz/tz.h"
#include "xsjo_compile.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace XER::Xejs {
namespace {

namespace fs = std::filesystem;

void PutU16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xff));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xff));
}
void PutU32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (unsigned s = 0; s < 32; s += 8) {
        v.push_back(static_cast<std::uint8_t>((x >> s) & 0xff));
    }
}
void PutU64(std::vector<std::uint8_t>& v, std::uint64_t x) {
    for (unsigned s = 0; s < 64; s += 8) {
        v.push_back(static_cast<std::uint8_t>((x >> s) & 0xff));
    }
}

std::uint16_t GetU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t GetU32(const std::uint8_t* p) {
    std::uint32_t x = 0;
    for (unsigned s = 0; s < 32; s += 8) {
        x |= static_cast<std::uint32_t>(p[s / 8]) << s;
    }
    return x;
}
std::uint64_t GetU64(const std::uint8_t* p) {
    std::uint64_t x = 0;
    for (unsigned s = 0; s < 64; s += 8) {
        x |= static_cast<std::uint64_t>(p[s / 8]) << s;
    }
    return x;
}

// Stems become mobjective/lib_<stem>.xsjo member names — keep them
// inside the charset the JIT loader's member scan accepts.
bool IsSafeStem(const std::string& stem) {
    if (stem.empty()) return false;
    for (const char c : stem) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

std::string ReadFileBytes(const fs::path& p, bool* ok) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        *ok = false;
        return {};
    }
    *ok = true;
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

}  // namespace

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string JsonGetString(const std::string& json,
                          const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto at = json.find(needle);
    if (at == std::string::npos) return {};
    at = json.find(':', at + needle.size());
    if (at == std::string::npos) return {};
    at = json.find('"', at + 1);
    if (at == std::string::npos) return {};
    std::string out;
    for (std::size_t i = at + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"') return out;
        if (c == '\\' && i + 1 < json.size()) {
            const char n = json[i + 1];
            if (n == 'n')      { out.push_back('\n'); ++i; continue; }
            if (n == 't')      { out.push_back('\t'); ++i; continue; }
            if (n == 'r')      { out.push_back('\r'); ++i; continue; }
            if (n == 'u' && i + 5 < json.size()) {
                // \uXXXX — only ASCII escapes are decoded; anything
                // else arrives as the raw bytes, which is fine for
                // the ASCII-only fields this scanner reads.
                const long cp = std::strtol(
                    json.substr(i + 2, 4).c_str(), nullptr, 16);
                if (cp > 0 && cp < 128) {
                    out.push_back(static_cast<char>(cp));
                    i += 5;
                    continue;
                }
            }
            out.push_back(n);
            ++i;
            continue;
        }
        out.push_back(c);
    }
    return {};
}

std::vector<std::uint8_t> PackHeadersBlob(
    const std::vector<HeaderEntry>& entries,
    const std::string& include_root_label) {
    // index.json — the manifest consumers/tooling read for a listing.
    std::string index = "{\"include_root\":\"" +
                        JsonEscape(include_root_label) +
                        "\",\"headers\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i != 0) index += ',';
        index += '"';
        index += JsonEscape(entries[i].name);
        index += '"';
    }
    index += "]}";

    std::vector<HeaderEntry> all;
    all.reserve(entries.size() + 1);
    all.push_back(HeaderEntry{"index.json",
                              std::vector<std::uint8_t>(
                                  index.begin(), index.end())});
    all.insert(all.end(), entries.begin(), entries.end());

    std::vector<std::uint8_t> blob;
    PutU32(blob, kHeadMagic);
    PutU16(blob, kVersion);
    PutU16(blob, 0);
    PutU32(blob, static_cast<std::uint32_t>(all.size()));
    for (const HeaderEntry& e : all) {
        PutU16(blob, static_cast<std::uint16_t>(e.name.size()));
        blob.insert(blob.end(), e.name.begin(), e.name.end());
        PutU64(blob, static_cast<std::uint64_t>(e.bytes.size()));
        blob.insert(blob.end(), e.bytes.begin(), e.bytes.end());
    }
    return blob;
}

bool UnpackHeadersBlob(const std::vector<std::uint8_t>& blob,
                       std::vector<HeaderEntry>* entries,
                       std::string* err) {
    const std::uint8_t* p = blob.data();
    const std::size_t n = blob.size();
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };
    if (n < 4 || GetU32(p) != kHeadMagic) {
        return fail("bad XJSH magic");
    }
    if (n < 12) return fail("truncated XJSH header");
    if (GetU16(p + 4) != kVersion) {
        return fail("unsupported XJSH version");
    }
    const std::uint32_t count = GetU32(p + 8);
    std::size_t at = 12;
    entries->clear();
    entries->reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (at + 2 > n) return fail("truncated XJSH entry name");
        const std::uint16_t name_len = GetU16(p + at);
        at += 2;
        if (at + name_len > n) return fail("truncated XJSH name");
        HeaderEntry e;
        e.name.assign(reinterpret_cast<const char*>(p + at),
                      name_len);
        at += name_len;
        if (at + 8 > n) return fail("truncated XJSH entry size");
        const std::uint64_t size = GetU64(p + at);
        at += 8;
        if (size > n - at) {
            return fail("XJSH entry overruns the blob");
        }
        e.bytes.assign(p + at, p + at + size);
        at += static_cast<std::size_t>(size);
        entries->push_back(std::move(e));
    }
    if (at != n) return fail("XJSH trailing bytes");
    return true;
}

bool CollectHeaders(const std::string& include_root,
                    std::vector<HeaderEntry>* entries,
                    std::string* err) {
    entries->clear();
    std::error_code ec;
    const fs::path root(include_root);
    if (!fs::is_directory(root, ec)) {
        return true;   // no include dir -> empty bundle, not an error
    }
    auto is_header = [](const fs::path& p) {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) {
                           return std::tolower(c);
                       });
        return ext == ".h" || ext == ".hpp" || ext == ".hxx" ||
               ext == ".inc";
    };
    for (const auto& de : fs::recursive_directory_iterator(root, ec)) {
        if (!de.is_regular_file(ec) || !is_header(de.path())) {
            continue;
        }
        const fs::path rel = fs::relative(de.path(), root, ec);
        if (ec || rel.empty()) continue;
        const std::string norm =
            Tz::TzNormalizeName(rel.generic_string());
        if (norm.empty()) {
            if (err) {
                *err = "unsafe header name: " +
                       de.path().generic_string();
            }
            return false;
        }
        bool ok = false;
        const std::string bytes = ReadFileBytes(de.path(), &ok);
        if (!ok) {
            if (err) {
                *err = "cannot read header: " +
                       de.path().generic_string();
            }
            return false;
        }
        HeaderEntry e;
        e.name = norm;
        e.bytes.assign(bytes.begin(), bytes.end());
        entries->push_back(std::move(e));
    }
    std::sort(entries->begin(), entries->end(),
              [](const HeaderEntry& a, const HeaderEntry& b) {
                  return a.name < b.name;
              });
    return true;
}

bool ExtractHeaders(const std::vector<HeaderEntry>& entries,
                    const std::string& dest_dir, std::string* err) {
    std::error_code ec;
    const fs::path dest(dest_dir);
    for (const HeaderEntry& e : entries) {
        if (e.name.empty() || !Tz::TzIsSafeName(e.name)) {
            if (err) *err = "unsafe header name in bundle: " + e.name;
            return false;
        }
        const fs::path out = dest / fs::path(e.name);
        fs::create_directories(out.parent_path(), ec);
        if (ec) {
            if (err) {
                *err = "cannot create " + out.parent_path().string() +
                       ": " + ec.message();
            }
            return false;
        }
        std::ofstream of(out, std::ios::binary | std::ios::trunc);
        if (!of) {
            if (err) *err = "cannot write " + out.string();
            return false;
        }
        of.write(reinterpret_cast<const char*>(e.bytes.data()),
                 static_cast<std::streamsize>(e.bytes.size()));
        if (!of) {
            if (err) *err = "short write on " + out.string();
            return false;
        }
    }
    return true;
}

// ---- .xejslib ------------------------------------------------------------

namespace {

std::string BuildJsonHeader(const Lib& lib) {
    std::string j = "{\"name\":\"" + JsonEscape(lib.name) +
                    "\",\"version\":\"" + JsonEscape(lib.version) +
                    "\",\"triple\":\"" + JsonEscape(lib.triple) +
                    "\",\"modules\":[";
    for (std::size_t i = 0; i < lib.modules.size(); ++i) {
        const Module& m = lib.modules[i];
        if (i != 0) j += ',';
        j += "{\"stem\":\"" + JsonEscape(m.stem) +
             "\",\"source\":\"" + JsonEscape(m.source_name) +
             "\",\"size\":" +
             std::to_string(
                 static_cast<unsigned long long>(
                     m.xsjo_bytes.size())) +
             "}";
    }
    j += "],\"headers\":{\"count\":" +
         std::to_string(lib.headers.size()) + ",\"entries\":[";
    for (std::size_t i = 0; i < lib.headers.size(); ++i) {
        if (i != 0) j += ',';
        j += '"';
        j += JsonEscape(lib.headers[i].name);
        j += '"';
    }
    j += "]}";
    return j;
}

}  // namespace

bool WriteLibFile(const Lib& lib, const std::string& out_path,
                  std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    if (lib.modules.empty()) {
        return fail("a .xejslib needs at least one module");
    }
    for (const Module& m : lib.modules) {
        if (!IsSafeStem(m.stem)) {
            return fail("unsafe module stem: " + m.stem);
        }
        Xsjo::XsjoInfo info;
        std::string perr;
        if (!Xsjo::ParseXsjo(m.xsjo_bytes, &info, &perr)) {
            return fail("module " + m.stem +
                        " is not a valid .xsjo: " + perr);
        }
    }
    const std::string json = BuildJsonHeader(lib);
    const std::vector<std::uint8_t> head =
        PackHeadersBlob(lib.headers, lib.name);

    std::vector<std::uint8_t> blob;
    blob.reserve(32 + json.size() + head.size());
    PutU32(blob, kLibMagic);
    PutU16(blob, kVersion);
    PutU16(blob, 0);
    PutU64(blob, static_cast<std::uint64_t>(json.size()));
    blob.insert(blob.end(), json.begin(), json.end());
    PutU64(blob, static_cast<std::uint64_t>(head.size()));
    blob.insert(blob.end(), head.begin(), head.end());
    PutU64(blob, static_cast<std::uint64_t>(lib.modules.size()));
    for (const Module& m : lib.modules) {
        PutU64(blob, static_cast<std::uint64_t>(m.xsjo_bytes.size()));
        blob.insert(blob.end(), m.xsjo_bytes.begin(),
                    m.xsjo_bytes.end());
    }

    std::error_code ec;
    const fs::path out(out_path);
    if (out.has_parent_path()) {
        fs::create_directories(out.parent_path(), ec);
    }
    std::ofstream of(out, std::ios::binary | std::ios::trunc);
    if (!of) return fail("cannot write " + out_path);
    of.write(reinterpret_cast<const char*>(blob.data()),
             static_cast<std::streamsize>(blob.size()));
    if (!of) return fail("short write on " + out_path);
    return true;
}

bool ReadLibFile(const std::string& path, Lib* lib, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };
    bool ok = false;
    const std::string raw = ReadFileBytes(path, &ok);
    if (!ok) return fail("cannot read " + path);
    const std::uint8_t* p =
        reinterpret_cast<const std::uint8_t*>(raw.data());
    const std::size_t n = raw.size();
    if (n < 4 || GetU32(p) != kLibMagic) {
        return fail(path + ": bad XJSL magic");
    }
    if (n < 8) return fail(path + ": truncated XJSL header");
    if (GetU16(p + 4) != kVersion) {
        return fail(path + ": unsupported XJSL version");
    }
    std::size_t at = 8;
    auto take_u64 = [&](std::uint64_t* out) {
        if (at + 8 > n) return false;
        *out = GetU64(p + at);
        at += 8;
        return true;
    };
    std::uint64_t json_len = 0;
    if (!take_u64(&json_len) || json_len > n - at) {
        return fail(path + ": truncated XJSL json header");
    }
    lib->json.assign(reinterpret_cast<const char*>(p + at),
                     static_cast<std::size_t>(json_len));
    at += static_cast<std::size_t>(json_len);
    std::uint64_t head_len = 0;
    if (!take_u64(&head_len) || head_len > n - at) {
        return fail(path + ": truncated XJSL header bundle");
    }
    if (head_len > 0) {
        const std::vector<std::uint8_t> blob(
            p + at, p + at + head_len);
        if (!UnpackHeadersBlob(blob, &lib->headers, err)) {
            if (err) *err = path + ": " + *err;
            return false;
        }
    } else {
        lib->headers.clear();
    }
    at += static_cast<std::size_t>(head_len);
    std::uint64_t module_count = 0;
    if (!take_u64(&module_count)) {
        return fail(path + ": missing XJSL module count");
    }
    lib->modules.clear();
    lib->modules.reserve(static_cast<std::size_t>(module_count));
    std::set<std::string> stems;
    for (std::uint64_t i = 0; i < module_count; ++i) {
        std::uint64_t size = 0;
        if (!take_u64(&size) || size > n - at) {
            return fail(path + ": truncated XJSL module payload");
        }
        Module m;
        m.xsjo_bytes.assign(p + at, p + at + size);
        at += static_cast<std::size_t>(size);
        // The embedded XSJ1 is the source of truth for the member
        // name — validate it here so consumers can splice the bytes
        // straight into mobjective/.
        Xsjo::XsjoInfo info;
        std::string perr;
        if (!Xsjo::ParseXsjo(m.xsjo_bytes, &info, &perr)) {
            return fail(path + ": module " + std::to_string(i) +
                        " is not a valid .xsjo: " + perr);
        }
        m.source_name = info.source_name;
        m.stem =
            fs::path(info.source_name).stem().generic_string();
        if (!IsSafeStem(m.stem)) {
            return fail(path + ": unsafe module stem '" + m.stem +
                        "'");
        }
        if (!stems.insert(m.stem).second) {
            return fail(path + ": duplicate module stem '" +
                        m.stem + "'");
        }
        lib->modules.push_back(std::move(m));
    }
    if (at != n) return fail(path + ": XJSL trailing bytes");
    if (lib->modules.empty()) {
        return fail(path + ": .xejslib carries no modules");
    }
    lib->name = JsonGetString(lib->json, "name");
    lib->version = JsonGetString(lib->json, "version");
    lib->triple = JsonGetString(lib->json, "triple");
    return true;
}

// ---- shared tool helpers ---------------------------------------------------

std::string ExeDir(const char* argv0) {
#ifdef _WIN32
    // argv[0] can be a bare name resolved via PATH — ask the loader
    // where this exe actually lives.
    wchar_t buf[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) != 0) {
        return fs::path(buf).parent_path().string();
    }
#endif
    std::error_code ec;
    const fs::path exe = fs::canonical(
        fs::path(argv0).has_parent_path()
            ? fs::absolute(argv0, ec)
            : fs::path(argv0),
        ec);
    if (!ec && !exe.empty()) return exe.parent_path().string();
    return fs::current_path(ec).string();
}

std::vector<std::string> ResolveSdkDirs(
    const std::string& exe_dir, const std::string& input_dir,
    const std::string& explicit_sdk) {
    const fs::path exe(exe_dir), input(input_dir);
    std::vector<std::string> out;
    std::set<std::string> seen;
    std::error_code ec;
    auto add = [&](const fs::path& p) {
        if (!fs::is_directory(p, ec)) return;
        const auto key = fs::weakly_canonical(p, ec).string();
        if (!key.empty() && seen.insert(key).second) {
            out.push_back(key);
        }
    };
    if (!explicit_sdk.empty()) add(explicit_sdk);
    if (const char* env = std::getenv("XER_VE_SDK");
        env != nullptr && *env != '\0') {
        add(env);
    }
    add(exe / "sdk");
    add(exe / "assets" / "sdk");
    add(exe.parent_path() / "sdk");
    // Dev-layout fallback: <build>/bin/../.. is the repo root.
    add(exe / ".." / ".." / ".." / "tools" / "video_editor" / "sdk");
    add(input / "sdk");
    add(input / ".." / "sdk");               // extensions_src/sdk
    add(input / ".." / ".." / "sdk");        // assets/sdk
    // tools/video_editor/assets/extensions_src/<ext>/../../../sdk
    // -> tools/video_editor/sdk — the in-tree SDK location.
    add(input / ".." / ".." / ".." / "sdk");
    return out;
}

}  // namespace XER::Xejs
