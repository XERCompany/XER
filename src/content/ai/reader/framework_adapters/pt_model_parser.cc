#include "pt_model_parser.h"

#include "../../models_builder/model_core/dense_layer.h"

#include <algorithm>
#include <cstring>

namespace XER::ModelsBuilder::Reader::Framework {

namespace {

// ZIP structures (all little-endian).
struct ZipLocalHeader {
    uint32_t signature;       // 0x04034B50
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time, mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_len;
    uint16_t extra_len;
};

struct ZipCentralDir {
    uint32_t signature;       // 0x02014B50
    uint16_t version_made, version_needed;
    uint16_t flags, compression;
    uint16_t mod_time, mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t int_attr;
    uint32_t ext_attr;
    uint32_t local_offset;
};

static uint16_t Read16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
static uint32_t Read32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])        |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16)|
           (static_cast<uint32_t>(p[3]) << 24);
}

// Locate End-of-Central-Directory record (signature 0x06054B50).
static bool FindEOCD(const std::vector<uint8_t>& buf, size_t& eocd_pos) {
    if (buf.size() < 22) return false;
    for (size_t i = buf.size() - 22; ; --i) {
        if (Read32LE(buf.data() + i) == 0x06054B50) {
            eocd_pos = i;
            return true;
        }
        if (i == 0) break;
    }
    return false;
}

// Collect all entry filenames from the central directory.
static std::vector<std::string> ListZipEntries(const std::vector<uint8_t>& buf) {
    std::vector<std::string> names;
    size_t eocd_pos = 0;
    if (!FindEOCD(buf, eocd_pos)) return names;

    const uint8_t* eocd = buf.data() + eocd_pos;
    const uint32_t cd_offset = Read32LE(eocd + 16);
    const uint16_t entry_count = Read16LE(eocd + 10);

    size_t pos = cd_offset;
    for (int i = 0; i < entry_count && pos + 46 <= buf.size(); ++i) {
        if (Read32LE(buf.data() + pos) != 0x02014B50) break;
        const uint16_t fn_len   = Read16LE(buf.data() + pos + 28);
        const uint16_t ex_len   = Read16LE(buf.data() + pos + 30);
        const uint16_t cm_len   = Read16LE(buf.data() + pos + 32);
        if (pos + 46 + fn_len > buf.size()) break;
        names.emplace_back(reinterpret_cast<const char*>(buf.data() + pos + 46), fn_len);
        pos += 46 + fn_len + ex_len + cm_len;
    }
    return names;
}

// Very lightweight pickle opcode scan:
// Look for GLOBAL opcode (0x63) followed by "torch.nn.modules.linear\nLinear\n"
// to count Linear layer instantiations.
static int CountLinearLayers(const std::vector<uint8_t>& buf,
                              const std::vector<std::string>& entries) {
    // Find the data.pkl entry offset
    size_t pkl_offset = 0, pkl_size = 0;
    for (const auto& name : entries) {
        if (name.find("data.pkl") != std::string::npos ||
            name.find("archive/constants.pkl") != std::string::npos) {
            // Walk local headers to find offset
            size_t lpos = 0;
            while (lpos + 30 <= buf.size()) {
                if (Read32LE(buf.data() + lpos) != 0x04034B50) { ++lpos; continue; }
                const uint16_t fn_len = Read16LE(buf.data() + lpos + 26);
                const uint16_t ex_len = Read16LE(buf.data() + lpos + 28);
                if (lpos + 30 + fn_len > buf.size()) break;
                const std::string fn(
                    reinterpret_cast<const char*>(buf.data() + lpos + 30), fn_len);
                const uint32_t comp_size = Read32LE(buf.data() + lpos + 18);
                const size_t data_start  = lpos + 30 + fn_len + ex_len;
                if (fn == name) {
                    pkl_offset = data_start;
                    pkl_size   = comp_size;
                    break;
                }
                lpos = data_start + comp_size;
            }
            if (pkl_size) break;
        }
    }

    if (!pkl_size || pkl_offset + pkl_size > buf.size()) return 0;

    // Scan for the string "Linear" (class name in pickle GLOBAL opcodes).
    static const char kLinear[] = "Linear";
    int count = 0;
    const uint8_t* pkl = buf.data() + pkl_offset;
    for (size_t i = 0; i + 6 <= pkl_size; ++i) {
        if (std::memcmp(pkl + i, kLinear, 6) == 0) {
            // Make sure it's not "LinearModel" or "LinearAlgebra" etc.
            const uint8_t after = (i + 6 < pkl_size) ? pkl[i + 6] : 0;
            if (after == '\n' || after == 0 || after == '\'') ++count;
        }
    }
    return count;
}

}  // namespace

LoadResult PtModelParser::Load(const std::string& filepath) {
    std::vector<uint8_t> buf;
    if (!ReadFile(filepath, buf))
        return {nullptr, false, "Cannot read: " + filepath};

    if (!HasZipMagic(buf))
        return {nullptr, false, "Not a valid PyTorch archive: " + filepath};

    const auto entries = ListZipEntries(buf);

    const int linear_count = CountLinearLayers(buf, entries);
    const int dense_count  = std::max(1, linear_count);

    auto model = std::make_shared<Core::Model>("pytorch_model");
    for (int i = 0; i < dense_count; ++i)
        model->AddLayer(std::make_shared<Core::DenseLayer>(256U));

    model->Build({1U, 256U});
    model->Compile();
    return {model, true, ""};
}

bool PtModelParser::CanLoad(const std::string& filepath) const {
    auto n = filepath.size();
    return (n >= 3 && filepath.substr(n - 3) == ".pt") ||
           (n >= 4 && filepath.substr(n - 4) == ".pth");
}

bool PtModelParser::HasZipMagic(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return false;
    return data[0] == 'P' && data[1] == 'K' &&
           (data[2] == 0x03 || data[2] == 0x05 || data[2] == 0x07);
}

}  // namespace XER::ModelsBuilder::Reader::Framework
