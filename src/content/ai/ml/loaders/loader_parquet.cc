#include "loader_parquet.h"

#include <cstring>
#include <fstream>
#include <algorithm>

namespace XER::ML::Loaders {

namespace {

// ---------------------------------------------------------------------------
// Thrift Binary Protocol helpers (Parquet uses Thrift Binary, big-endian).
// ---------------------------------------------------------------------------

// Thrift type constants
static constexpr uint8_t kThriftBool   = 2;
static constexpr uint8_t kThriftByte   = 3;
static constexpr uint8_t kThriftDouble = 4;
static constexpr uint8_t kThriftI16    = 6;
static constexpr uint8_t kThriftI32    = 8;
static constexpr uint8_t kThriftI64    = 10;
static constexpr uint8_t kThriftString = 11;
static constexpr uint8_t kThriftStruct = 12;
static constexpr uint8_t kThriftList   = 15;
static constexpr uint8_t kThriftStop   = 0;

static inline uint16_t Read16BE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] << 8) | p[1];
}
static inline uint32_t Read32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}
static inline uint64_t Read64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(Read32BE(p)) << 32) | Read32BE(p + 4);
}
static inline uint32_t Read32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])         |
           (static_cast<uint32_t>(p[1]) <<  8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Skip one Thrift field value of the given type; returns false on overrun.
static bool ThriftSkip(const uint8_t* data, size_t size, size_t& pos,
                        uint8_t type);

static bool ThriftSkipStruct(const uint8_t* data, size_t size, size_t& pos) {
    while (pos < size) {
        const uint8_t t = data[pos++];
        if (t == kThriftStop) return true;
        if (pos + 2 > size) return false;
        pos += 2;  // skip field id
        if (!ThriftSkip(data, size, pos, t)) return false;
    }
    return false;
}

static bool ThriftSkip(const uint8_t* data, size_t size, size_t& pos,
                        uint8_t type) {
    switch (type) {
        case kThriftBool:   ++pos; return pos <= size;
        case kThriftByte:   ++pos; return pos <= size;
        case kThriftI16:    pos += 2; return pos <= size;
        case kThriftI32:    pos += 4; return pos <= size;
        case kThriftI64:    pos += 8; return pos <= size;
        case kThriftDouble: pos += 8; return pos <= size;
        case kThriftString: {
            if (pos + 4 > size) return false;
            const uint32_t len = Read32BE(data + pos); pos += 4;
            pos += len;
            return pos <= size;
        }
        case kThriftStruct: return ThriftSkipStruct(data, size, pos);
        case kThriftList: {
            if (pos + 5 > size) return false;
            const uint8_t  elem_type = data[pos++];
            const uint32_t count     = Read32BE(data + pos); pos += 4;
            for (uint32_t i = 0; i < count; ++i)
                if (!ThriftSkip(data, size, pos, elem_type)) return false;
            return true;
        }
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// Parquet column/page structures.
// ---------------------------------------------------------------------------

struct ColumnMeta {
    int type = 0;                   // 0=BOOLEAN,1=INT32,2=INT64,4=FLOAT,5=DOUBLE,6=BYTE_ARRAY
    int64_t data_page_offset = 0;
    int64_t total_compressed_size = 0;
    int64_t num_values = 0;
    int codec = 0;                  // 0=UNCOMPRESSED,1=SNAPPY,...
};

// Parse a ColumnMetaData Thrift struct.
static bool ParseColumnMeta(const uint8_t* data, size_t size, size_t& pos,
                              ColumnMeta& out) {
    while (pos < size) {
        if (pos + 1 > size) return false;
        const uint8_t t = data[pos++];
        if (t == kThriftStop) return true;
        if (pos + 2 > size) return false;
        const uint16_t fid = Read16BE(data + pos); pos += 2;
        switch (fid) {
            case 1: if (t == kThriftI32 && pos + 4 <= size) { out.type = static_cast<int>(Read32BE(data+pos)); pos += 4; } else if (!ThriftSkip(data,size,pos,t)) return false; break;
            case 5: if (t == kThriftI64 && pos + 8 <= size) { out.num_values = static_cast<int64_t>(Read64BE(data+pos)); pos += 8; } else if (!ThriftSkip(data,size,pos,t)) return false; break;
            case 4: if (t == kThriftI32 && pos + 4 <= size) { out.codec = static_cast<int>(Read32BE(data+pos)); pos += 4; } else if (!ThriftSkip(data,size,pos,t)) return false; break;
            case 9: if (t == kThriftI64 && pos + 8 <= size) { out.data_page_offset = static_cast<int64_t>(Read64BE(data+pos)); pos += 8; } else if (!ThriftSkip(data,size,pos,t)) return false; break;
            case 12: if (t == kThriftI64 && pos + 8 <= size) { out.total_compressed_size = static_cast<int64_t>(Read64BE(data+pos)); pos += 8; } else if (!ThriftSkip(data,size,pos,t)) return false; break;
            default: if (!ThriftSkip(data, size, pos, t)) return false; break;
        }
    }
    return false;
}

// Parse a ColumnChunk struct (field 3 = ColumnMetaData).
static bool ParseColumnChunk(const uint8_t* data, size_t size, size_t& pos,
                               ColumnMeta& out) {
    while (pos < size) {
        const uint8_t t = data[pos++];
        if (t == kThriftStop) return true;
        if (pos + 2 > size) return false;
        const uint16_t fid = Read16BE(data + pos); pos += 2;
        if (fid == 3 && t == kThriftStruct) {
            if (!ParseColumnMeta(data, size, pos, out)) return false;
        } else {
            if (!ThriftSkip(data, size, pos, t)) return false;
        }
    }
    return false;
}

// Parse a RowGroup struct: field 1 = list<ColumnChunk>, field 3 = num_rows.
static bool ParseRowGroup(const uint8_t* data, size_t size, size_t& pos,
                           std::vector<ColumnMeta>& cols, int64_t& num_rows) {
    while (pos < size) {
        const uint8_t t = data[pos++];
        if (t == kThriftStop) return true;
        if (pos + 2 > size) return false;
        const uint16_t fid = Read16BE(data + pos); pos += 2;
        if (fid == 1 && t == kThriftList) {
            if (pos + 5 > size) return false;
            const uint8_t  et    = data[pos++];
            const uint32_t count = Read32BE(data + pos); pos += 4;
            for (uint32_t i = 0; i < count; ++i) {
                if (et == kThriftStruct) {
                    ColumnMeta cm;
                    if (!ParseColumnChunk(data, size, pos, cm)) return false;
                    cols.push_back(cm);
                } else {
                    if (!ThriftSkip(data, size, pos, et)) return false;
                }
            }
        } else if (fid == 3 && t == kThriftI64 && pos + 8 <= size) {
            num_rows = static_cast<int64_t>(Read64BE(data + pos)); pos += 8;
        } else {
            if (!ThriftSkip(data, size, pos, t)) return false;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// PageHeader parser (also Thrift Binary).
// Returns num_values for a DATA_PAGE (type 0) or -1 if dictionary page.
// ---------------------------------------------------------------------------
struct PageInfo {
    int type = 0;               // 0=DATA_PAGE, 2=DICTIONARY_PAGE
    int32_t uncompressed_size = 0;
    int32_t compressed_size   = 0;
    int32_t num_values        = 0;
    size_t  header_byte_size  = 0;
};

static bool ParsePageHeader(const uint8_t* data, size_t size, PageInfo& info) {
    size_t pos = 0;
    while (pos < size) {
        const uint8_t t = data[pos++];
        if (t == kThriftStop) { info.header_byte_size = pos; return true; }
        if (pos + 2 > size) return false;
        const uint16_t fid = Read16BE(data + pos); pos += 2;
        switch (fid) {
            case 1: if (t == kThriftI32 && pos + 4 <= size) { info.type = static_cast<int32_t>(Read32BE(data+pos)); pos += 4; } else ThriftSkip(data,size,pos,t); break;
            case 2: if (t == kThriftI32 && pos + 4 <= size) { info.uncompressed_size = static_cast<int32_t>(Read32BE(data+pos)); pos += 4; } else ThriftSkip(data,size,pos,t); break;
            case 3: if (t == kThriftI32 && pos + 4 <= size) { info.compressed_size   = static_cast<int32_t>(Read32BE(data+pos)); pos += 4; } else ThriftSkip(data,size,pos,t); break;
            case 5: {
                // DataPageHeader struct — field 1 = num_values
                if (t != kThriftStruct) { ThriftSkip(data,size,pos,t); break; }
                while (pos < size) {
                    const uint8_t st = data[pos++];
                    if (st == kThriftStop) break;
                    if (pos + 2 > size) return false;
                    const uint16_t sfid = Read16BE(data + pos); pos += 2;
                    if (sfid == 1 && st == kThriftI32 && pos + 4 <= size) {
                        info.num_values = static_cast<int32_t>(Read32BE(data+pos)); pos += 4;
                    } else ThriftSkip(data, size, pos, st);
                }
                break;
            }
            default: ThriftSkip(data, size, pos, t); break;
        }
    }
    info.header_byte_size = pos;
    return true;
}

// ---------------------------------------------------------------------------
// Read float values from a PLAIN-encoded data page (Parquet FLOAT type = 4).
// ---------------------------------------------------------------------------
static void ReadPlainFloats(const uint8_t* page_data, size_t page_size,
                             int32_t num_values,
                             std::vector<float>& out) {
    // In practice, definition and repetition level bytes may precede data.
    // For a flat (required, non-nested) schema they are 0 bytes.
    const size_t expected = static_cast<size_t>(num_values) * 4;
    if (expected > page_size) return;
    const size_t offset = page_size - expected;  // skip level bytes
    for (int32_t i = 0; i < num_values; ++i) {
        uint32_t raw = Read32LE(page_data + offset + i * 4);
        float v;
        std::memcpy(&v, &raw, 4);
        out.push_back(v);
    }
}

// ---------------------------------------------------------------------------
// Read all float columns from a Parquet file (UNCOMPRESSED / PLAIN encoding).
// ---------------------------------------------------------------------------
static bool ReadParquetFloatColumns(const std::string& path,
                                     std::vector<std::vector<float>>& columns,
                                     int64_t& num_rows_out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // Read entire file into buffer.
    f.seekg(0, std::ios::end);
    const auto file_size = static_cast<size_t>(f.tellg());
    if (file_size < 12) return false;
    f.seekg(0);
    std::vector<uint8_t> buf(file_size);
    if (!f.read(reinterpret_cast<char*>(buf.data()), file_size)) return false;

    // Validate PAR1 magic at start and end.
    if (std::memcmp(buf.data(), "PAR1", 4) != 0) return false;
    if (std::memcmp(buf.data() + file_size - 4, "PAR1", 4) != 0) return false;

    // Footer length (4 bytes before trailing PAR1, little-endian).
    const uint32_t footer_len = Read32LE(buf.data() + file_size - 8);
    if (footer_len == 0 || footer_len + 8 > file_size) return false;

    const size_t footer_start = file_size - 8 - footer_len;
    const uint8_t* footer     = buf.data() + footer_start;

    // Parse FileMetaData Thrift struct to find RowGroups.
    // Field 4 = list<RowGroup>
    std::vector<ColumnMeta> all_cols;
    num_rows_out = 0;
    {
        size_t pos = 0;
        while (pos < footer_len) {
            const uint8_t t = footer[pos++];
            if (t == kThriftStop) break;
            if (pos + 2 > footer_len) break;
            const uint16_t fid = Read16BE(footer + pos); pos += 2;
            if (fid == 3 && t == kThriftI64 && pos + 8 <= footer_len) {
                num_rows_out = static_cast<int64_t>(Read64BE(footer + pos));
                pos += 8;
            } else if (fid == 4 && t == kThriftList) {
                if (pos + 5 > footer_len) break;
                const uint8_t  et    = footer[pos++];
                const uint32_t count = Read32BE(footer + pos); pos += 4;
                for (uint32_t rg = 0; rg < count; ++rg) {
                    if (et != kThriftStruct) { ThriftSkip(footer, footer_len, pos, et); continue; }
                    std::vector<ColumnMeta> rg_cols;
                    int64_t rg_rows = 0;
                    if (!ParseRowGroup(footer, footer_len, pos, rg_cols, rg_rows)) break;
                    if (num_rows_out == 0) num_rows_out = rg_rows;
                    for (auto& c : rg_cols)
                        if (c.type == 4 /* FLOAT */) all_cols.push_back(c);
                }
            } else {
                if (!ThriftSkip(footer, footer_len, pos, t)) break;
            }
        }
    }

    if (all_cols.empty() || num_rows_out == 0) return false;

    // For each FLOAT column, read its pages and extract raw floats.
    columns.resize(all_cols.size());
    for (size_t ci = 0; ci < all_cols.size(); ++ci) {
        const ColumnMeta& cm = all_cols[ci];
        if (cm.codec != 0 || cm.data_page_offset <= 0) continue;  // skip compressed
        const auto off = static_cast<size_t>(cm.data_page_offset);
        size_t cur = off;
        const size_t col_end = off + static_cast<size_t>(cm.total_compressed_size);
        if (col_end > file_size) continue;

        while (cur < col_end) {
            // PageHeader at cur
            PageInfo pi;
            if (!ParsePageHeader(buf.data() + cur, col_end - cur, pi)) break;
            cur += pi.header_byte_size;
            const size_t page_data_size = static_cast<size_t>(pi.compressed_size);
            if (cur + page_data_size > col_end) break;
            if (pi.type == 0 /* DATA_PAGE */ && pi.num_values > 0) {
                ReadPlainFloats(buf.data() + cur, page_data_size,
                                pi.num_values, columns[ci]);
            }
            cur += page_data_size;
        }
    }

    return true;
}

}  // namespace

Dataset LoaderParquet::Load(const std::string& path) {
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    std::vector<std::vector<float>> columns;
    int64_t num_rows = 0;

    if (!ReadParquetFloatColumns(path, columns, num_rows)) {
        LogError("Failed to parse Parquet file: " + path);
        Dataset empty{};
        StoreCachedDataset(Name(), path, empty);
        return empty;
    }

    // Transpose columns → rows (X[row][feature]).
    const int n_features = static_cast<int>(columns.size());
    const int n_rows     = static_cast<int>(num_rows);
    Dataset ds;
    ds.X.resize(n_rows, std::vector<float>(n_features, 0.0f));
    for (int ci = 0; ci < n_features; ++ci) {
        const auto& col = columns[ci];
        for (int ri = 0; ri < n_rows && ri < static_cast<int>(col.size()); ++ri)
            ds.X[ri][ci] = col[ri];
    }

    LogInfo("ParquetLoader: loaded " + std::to_string(n_rows) +
            " rows × " + std::to_string(n_features) + " features from " + path);
    StoreCachedDataset(Name(), path, ds);
    return ds;
}

}  // namespace XER::ML::Loaders
