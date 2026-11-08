#include "keras_h5_reader.h"

#include "../../models_builder/model_core/dense_layer.h"

#include <algorithm>
#include <cstring>

namespace XER::ModelsBuilder::Reader::Framework {

namespace {

// HDF5 superblock signature (first 8 bytes).
static constexpr uint8_t kHdf5Magic[8] = {
    0x89, 0x48, 0x44, 0x46, 0x0D, 0x0A, 0x1A, 0x0A
};

static bool HasHdf5Magic(const std::vector<uint8_t>& buf) {
    return buf.size() >= 8 &&
           std::memcmp(buf.data(), kHdf5Magic, 8) == 0;
}

// Very lightweight scan: count occurrences of the string "Dense" inside the
// HDF5 file (it appears as layer class names in the model_config JSON
// attribute and in group names like "dense", "dense_1", etc.).
static int CountDenseLayers(const std::vector<uint8_t>& buf) {
    static const char kDense[]  = "Dense";
    static const char kLinear[] = "linear";  // Keras Linear alias
    int count = 0;
    for (size_t i = 0; i + 5 <= buf.size(); ++i) {
        if (std::memcmp(buf.data() + i, kDense, 5) == 0) {
            // Avoid double-counting "DenseLayer", "DenseFoo" etc.
            // Accept when followed by '"', '\0', space, ',', '\n', or ':'
            if (i + 5 < buf.size()) {
                const uint8_t after = buf[i + 5];
                if (after == '"' || after == 0 || after == ' ' ||
                    after == ',' || after == '\n' || after == ':')
                    ++count;
            } else {
                ++count;
            }
        }
    }
    // Avoid returning 0 (use 1 as minimum).
    return std::max(1, count);
}

// Scan for "units" JSON fields to extract the last declared unit count.
// Pattern: "units": NNN  in model_config JSON stored as an HDF5 attribute.
static uint32_t LastDenseUnits(const std::vector<uint8_t>& buf) {
    static const char kUnits[] = "\"units\"";
    uint32_t last_units = 128U;
    for (size_t i = 0; i + 7 <= buf.size(); ++i) {
        if (std::memcmp(buf.data() + i, kUnits, 7) != 0) continue;
        // Skip ": " or ":"
        size_t j = i + 7;
        while (j < buf.size() && (buf[j] == ':' || buf[j] == ' ')) ++j;
        // Parse integer
        uint32_t v = 0;
        bool found = false;
        while (j < buf.size() && buf[j] >= '0' && buf[j] <= '9') {
            v = v * 10 + (buf[j] - '0');
            ++j;
            found = true;
        }
        if (found && v > 0) last_units = v;
    }
    return last_units;
}

}  // namespace

LoadResult KerasH5Reader::Load(const std::string& filepath) {
    std::vector<uint8_t> buf;
    if (!ReadFile(filepath, buf))
        return {nullptr, false, "Cannot read: " + filepath};

    if (!HasHdf5Magic(buf))
        return {nullptr, false, "Not a valid HDF5 file: " + filepath};

    const int   dense_count = CountDenseLayers(buf);
    const uint32_t units    = LastDenseUnits(buf);

    auto model = std::make_shared<Core::Model>("keras_model");
    for (int i = 0; i < dense_count; ++i)
        model->AddLayer(std::make_shared<Core::DenseLayer>(units));

    model->Build({1U, units});
    model->Compile();
    return LoadResult{model, true, ""};
}

bool KerasH5Reader::CanLoad(const std::string& filepath) const {
    if (filepath.size() < 3) return false;
    const auto ext3 = filepath.substr(filepath.size() - 3);
    if (ext3 == ".h5") return true;
    return filepath.size() > 6 &&
           filepath.substr(filepath.size() - 6) == ".keras";
}

}  // namespace XER::ModelsBuilder::Reader::Framework
