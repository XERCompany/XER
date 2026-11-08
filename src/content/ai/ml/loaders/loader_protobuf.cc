#include "loader_protobuf.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>

namespace XER::ML::Loaders {

// ---------------------------------------------------------------------------
// TFRecord + minimal protobuf wire-format parser (no libprotobuf required).
//
// TFRecord per-record layout (little-endian):
//   [uint64 data_length][uint32 masked_crc32_of_length]
//   [data_length bytes  ][uint32 masked_crc32_of_data  ]
//
// data bytes = serialized tf.train.Example protobuf:
//   Example  { features: Features         = field 1 }
//   Features { feature: map<str,Feature>  = field 1 }
//   Feature  { float_list: FloatList      = field 2
//             | int64_list: Int64List      = field 3 }
//   FloatList{ value: repeated float      = field 1 (packed) }
//   Int64List{ value: repeated int64      = field 1 (packed) }
// ---------------------------------------------------------------------------
namespace {

// Read a protobuf varint from buf[*pos..end). Returns false on truncation.
bool ReadVarint64(const uint8_t* buf, size_t end,
                  size_t* pos, uint64_t* out) {
    uint64_t result = 0;
    int shift = 0;
    while (*pos < end) {
        const uint8_t b = buf[(*pos)++];
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) { *out = result; return true; }
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

// Skip one protobuf field of the given wire type. Returns false on error.
bool SkipField(const uint8_t* buf, size_t end,
               size_t* pos, int wire_type) {
    if (wire_type == 0) {
        uint64_t tmp;
        return ReadVarint64(buf, end, pos, &tmp);
    } else if (wire_type == 1) {
        if (*pos + 8 > end) return false;
        *pos += 8;
        return true;
    } else if (wire_type == 2) {
        uint64_t len;
        if (!ReadVarint64(buf, end, pos, &len)) return false;
        if (*pos + len > end) return false;
        *pos += static_cast<size_t>(len);
        return true;
    } else if (wire_type == 5) {
        if (*pos + 4 > end) return false;
        *pos += 4;
        return true;
    }
    return false;  // unknown wire type
}

// Parse packed repeated float32 (little-endian) from buf[start..start+len).
void ParsePackedFloats(const uint8_t* buf, size_t start, size_t len,
                       std::vector<float>& out) {
    const size_t end = start + len;
    for (size_t p = start; p + 4 <= end; p += 4) {
        uint32_t bits = 0;
        std::memcpy(&bits, buf + p, 4);
        float v;
        std::memcpy(&v, &bits, 4);
        out.push_back(v);
    }
}

// Parse packed repeated int64 (varint-encoded) from buf[start..start+len).
void ParsePackedInt64(const uint8_t* buf, size_t start, size_t len,
                      std::vector<int64_t>& out) {
    const size_t end = start + len;
    size_t pos = start;
    while (pos < end) {
        uint64_t v = 0;
        if (!ReadVarint64(buf, end, &pos, &v)) break;
        out.push_back(static_cast<int64_t>(v));
    }
}

// Parse a Feature message { float_list=2, int64_list=3 }.
// Fills row_floats from FloatList.value and row_ints from Int64List.value.
void ParseFeature(const uint8_t* buf, size_t start, size_t end,
                  std::vector<float>& row_floats,
                  std::vector<int64_t>& row_ints) {
    size_t pos = start;
    while (pos < end) {
        uint64_t tag = 0;
        if (!ReadVarint64(buf, end, &pos, &tag)) break;
        const int wire_type = static_cast<int>(tag & 0x7);
        const uint64_t field_num = tag >> 3;

        if (wire_type != 2) { SkipField(buf, end, &pos, wire_type); continue; }

        uint64_t field_len = 0;
        if (!ReadVarint64(buf, end, &pos, &field_len)) break;
        const size_t field_end = pos + static_cast<size_t>(field_len);
        if (field_end > end) break;

        // field 2 = float_list: FloatList, field 3 = int64_list: Int64List
        if (field_num == 2 || field_num == 3) {
            // inner message has field 1 = packed values
            size_t inner = pos;
            while (inner < field_end) {
                uint64_t inner_tag = 0;
                if (!ReadVarint64(buf, field_end, &inner, &inner_tag)) break;
                const int inner_wire = static_cast<int>(inner_tag & 0x7);
                if (inner_wire != 2) { SkipField(buf, field_end, &inner, inner_wire); continue; }
                uint64_t inner_len = 0;
                if (!ReadVarint64(buf, field_end, &inner, &inner_len)) break;
                if ((inner_tag >> 3) == 1) {  // value field
                    if (field_num == 2)
                        ParsePackedFloats(buf, inner, static_cast<size_t>(inner_len), row_floats);
                    else
                        ParsePackedInt64(buf, inner, static_cast<size_t>(inner_len), row_ints);
                }
                inner += static_cast<size_t>(inner_len);
            }
        }
        pos = field_end;
    }
}

// Recursively descend all length-delimited fields in a serialized tf.Example
// looking for Feature messages and extracting float/int64 values.
void ParseExampleBytes(const uint8_t* buf, size_t start, size_t end,
                       std::vector<float>& row_floats,
                       std::vector<int64_t>& row_ints) {
    size_t pos = start;
    while (pos < end) {
        uint64_t tag = 0;
        if (!ReadVarint64(buf, end, &pos, &tag)) break;
        const int wire_type = static_cast<int>(tag & 0x7);
        if (wire_type != 2) { SkipField(buf, end, &pos, wire_type); continue; }

        uint64_t field_len = 0;
        if (!ReadVarint64(buf, end, &pos, &field_len)) break;
        const size_t field_end = pos + static_cast<size_t>(field_len);
        if (field_end > end) break;

        // Try parsing this field as a Feature; also recurse into nested messages.
        ParseFeature(buf, pos, field_end, row_floats, row_ints);
        ParseExampleBytes(buf, pos, field_end, row_floats, row_ints);
        pos = field_end;
    }
}

// Read one TFRecord from stream. Returns false on clean EOF or fatal error.
bool ReadTFRecord(std::ifstream& f, std::vector<uint8_t>* data_out) {
    uint64_t data_len = 0;
    if (!f.read(reinterpret_cast<char*>(&data_len), 8)) return false;  // EOF

    uint32_t crc_len = 0;
    if (!f.read(reinterpret_cast<char*>(&crc_len), 4)) return false;
    // CRC validation omitted — trust well-formed files.

    data_out->resize(static_cast<size_t>(data_len));
    if (data_len > 0 &&
        !f.read(reinterpret_cast<char*>(data_out->data()),
                static_cast<std::streamsize>(data_len)))
        return false;

    uint32_t crc_data = 0;
    f.read(reinterpret_cast<char*>(&crc_data), 4);  // read and discard
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// LoaderProtobuf::Load — reads a TFRecord file without libprotobuf
// ---------------------------------------------------------------------------
Dataset LoaderProtobuf::Load(const std::string& path) {
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        const std::string msg =
            absl::StrFormat("ProtobufLoader: cannot open '%s'", path);
        LogError(msg);
        throw std::runtime_error(msg);
    }

    LogInfo(absl::StrFormat("ProtobufLoader: reading TFRecord '%s'", path));

    Dataset ds;
    std::vector<uint8_t> record_buf;
    while (ReadTFRecord(f, &record_buf)) {
        std::vector<float>   row_floats;
        std::vector<int64_t> row_ints;

        if (!record_buf.empty()) {
            ParseExampleBytes(record_buf.data(), 0, record_buf.size(),
                              row_floats, row_ints);
        }

        if (!row_floats.empty())
            ds.X.push_back(std::move(row_floats));

        if (!row_ints.empty())
            ds.y.push_back(static_cast<int>(row_ints.front()));
    }

    LogInfo(absl::StrFormat("ProtobufLoader: loaded %zu records from '%s'",
                            ds.X.size(), path));
    StoreCachedDataset(Name(), path, ds);
    return ds;
}

}  // namespace XER::ML::Loaders
