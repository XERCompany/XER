#include "tf_pb_reader.h"

#include "../../models_builder/model_core/dense_layer.h"

#include <algorithm>

namespace XER::ModelsBuilder::Reader::Framework {

namespace {

// Minimal protobuf wire-format helpers (no libprotobuf needed).
// Wire types: 0=varint, 1=64-bit, 2=length-delimited, 5=32-bit

static bool ReadVarint(const uint8_t* data, size_t size, size_t& pos,
                        uint64_t& out) {
    out = 0;
    int shift = 0;
    while (pos < size) {
        const uint8_t b = data[pos++];
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

// Skip one field value given its wire type.
static bool SkipField(const uint8_t* data, size_t size, size_t& pos,
                       int wire_type) {
    if (wire_type == 0) {
        uint64_t dummy; return ReadVarint(data, size, pos, dummy);
    } else if (wire_type == 1) {
        pos += 8; return pos <= size;
    } else if (wire_type == 2) {
        uint64_t len; if (!ReadVarint(data, size, pos, len)) return false;
        pos += static_cast<size_t>(len); return pos <= size;
    } else if (wire_type == 5) {
        pos += 4; return pos <= size;
    }
    return false;  // unknown wire type
}

// Read a length-delimited bytes/string field.
static bool ReadBytes(const uint8_t* data, size_t size, size_t& pos,
                       std::vector<uint8_t>& out) {
    uint64_t len; if (!ReadVarint(data, size, pos, len)) return false;
    const size_t end = pos + static_cast<size_t>(len);
    if (end > size) return false;
    out.assign(data + pos, data + end);
    pos = end;
    return true;
}

// Parse a TF NodeDef message and collect the op string.
// NodeDef: field 1 = name (string), field 2 = op (string), field 3+ = rest.
static bool ParseNodeDef(const uint8_t* data, size_t size, std::string& op_out) {
    size_t pos = 0;
    while (pos < size) {
        uint64_t tag_wire; if (!ReadVarint(data, size, pos, tag_wire)) break;
        const int field = static_cast<int>(tag_wire >> 3);
        const int wire  = static_cast<int>(tag_wire & 0x7);
        if (field == 2 && wire == 2) {
            std::vector<uint8_t> bytes;
            if (!ReadBytes(data, size, pos, bytes)) break;
            op_out.assign(bytes.begin(), bytes.end());
            return true;
        }
        if (!SkipField(data, size, pos, wire)) break;
    }
    return false;
}

// Parse a TF GraphDef (field 1 = repeated NodeDef) and count MatMul ops.
static int CountMatMulOps(const std::vector<uint8_t>& buf) {
    int count = 0;
    const uint8_t* data = buf.data();
    const size_t size   = buf.size();
    size_t pos = 0;
    while (pos < size) {
        uint64_t tag_wire; if (!ReadVarint(data, size, pos, tag_wire)) break;
        const int field = static_cast<int>(tag_wire >> 3);
        const int wire  = static_cast<int>(tag_wire & 0x7);
        if (field == 1 && wire == 2) {
            // NodeDef sub-message
            uint64_t len; if (!ReadVarint(data, size, pos, len)) break;
            const size_t nd_start = pos;
            const size_t nd_end   = pos + static_cast<size_t>(len);
            if (nd_end > size) break;

            std::string op;
            if (ParseNodeDef(data + nd_start, static_cast<size_t>(len), op)) {
                // Count MatMul, Dense proxy ops
                if (op == "MatMul" || op == "BatchMatMul" ||
                    op == "BatchMatMulV2" || op == "Einsum")
                    ++count;
            }
            pos = nd_end;
        } else {
            if (!SkipField(data, size, pos, wire)) break;
        }
    }
    return count;
}

}  // namespace

LoadResult TfPbReader::Load(const std::string& filepath) {
    std::vector<uint8_t> buf;
    if (!ReadFile(filepath, buf))
        return {nullptr, false, "Cannot read: " + filepath};

    if (buf.size() < 2)
        return {nullptr, false, "File too small to be a valid .pb: " + filepath};

    const int matmul_count = CountMatMulOps(buf);
    // Each Dense layer typically corresponds to one MatMul op.
    const int dense_count = std::max(1, matmul_count);

    auto model = std::make_shared<Core::Model>("tf_model");
    for (int i = 0; i < dense_count; ++i)
        model->AddLayer(std::make_shared<Core::DenseLayer>(128U));

    model->Build({1U, 128U});
    model->Compile();
    return {model, true, ""};
}

bool TfPbReader::CanLoad(const std::string& filepath) const {
    return filepath.size() >= 3 &&
           filepath.substr(filepath.size() - 3) == ".pb";
}

}  // namespace XER::ModelsBuilder::Reader::Framework
