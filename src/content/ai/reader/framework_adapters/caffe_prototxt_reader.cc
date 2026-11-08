#include "caffe_prototxt_reader.h"

#include "../../models_builder/model_core/dense_layer.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace XER::ModelsBuilder::Reader::Framework {

namespace {

// Trim leading/trailing whitespace and surrounding quotes from a value string.
std::string TrimValue(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (std::isspace((unsigned char)s[start]) || s[start] == '"')) ++start;
    while (end > start && (std::isspace((unsigned char)s[end - 1]) || s[end - 1] == '"')) --end;
    return s.substr(start, end - start);
}

// Describe a Caffe layer block as struct.
struct CaffeLayerDesc {
    std::string name;
    std::string type;
    int num_output = 0;
};

// Parse all layer {} blocks from Caffe prototxt text.
std::vector<CaffeLayerDesc> ParseProtoxtLayers(const std::string& text) {
    std::vector<CaffeLayerDesc> layers;
    std::istringstream stream(text);
    std::string line;
    int depth = 0;
    bool in_layer = false;
    bool in_param = false;  // inside *_param sub-block
    CaffeLayerDesc current;

    while (std::getline(stream, line)) {
        // Strip inline comment
        const auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);

        for (char ch : line) {
            if (ch == '{') {
                ++depth;
                if (depth == 1) { in_layer = true; current = {}; }
                else if (depth == 2) in_param = true;
            } else if (ch == '}') {
                if (depth == 2) in_param = false;
                else if (depth == 1) {
                    if (in_layer && !current.type.empty())
                        layers.push_back(current);
                    in_layer = false;
                }
                if (depth > 0) --depth;
            }
        }

        if (!in_layer) continue;

        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        // trim key
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        const std::string val = TrimValue(line.substr(colon + 1));

        if (!in_param) {
            if (key == "name") current.name = val;
            else if (key == "type") current.type = val;
        } else {
            if (key == "num_output") {
                try { current.num_output = std::stoi(val); } catch (...) {}
            }
        }
    }
    return layers;
}

}  // namespace

LoadResult CaffeProtoxtReader::Load(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open())
        return {nullptr, false, "Cannot open: " + filepath};

    std::ostringstream buf;
    buf << f.rdbuf();
    const std::string text = buf.str();

    const auto descs = ParseProtoxtLayers(text);
    if (descs.empty())
        return {nullptr, false, "No layers found in prototxt: " + filepath};

    // Derive a model name from the net name field if present
    std::string model_name = "caffe_model";
    {
        const auto pos = text.find("name:");
        if (pos != std::string::npos) {
            const auto nl = text.find('\n', pos);
            const std::string line = text.substr(pos, nl - pos);
            const auto colon = line.find(':');
            if (colon != std::string::npos)
                model_name = TrimValue(line.substr(colon + 1));
        }
    }

    auto model = std::make_shared<Core::Model>(model_name);
    uint32_t last_dim = 0;

    for (const auto& d : descs) {
        const std::string t = d.type;
        if (t == "InnerProduct" || t == "FC") {
            const uint32_t units = d.num_output > 0
                ? static_cast<uint32_t>(d.num_output) : 128U;
            model->AddLayer(std::make_shared<Core::DenseLayer>(units));
            last_dim = units;
        }
        // Convolution, Pooling, BatchNorm, ReLU, Dropout, SoftmaxWithLoss etc.
        // are skipped — they need dedicated layer types not yet in model_core.
    }

    // If no InnerProduct layers were found, add one placeholder dense layer.
    if (last_dim == 0) {
        model->AddLayer(std::make_shared<Core::DenseLayer>(96U));
        last_dim = 96U;
    }

    model->Build({1U, last_dim});
    model->Compile();
    return LoadResult{model, true, ""};
}

bool CaffeProtoxtReader::CanLoad(const std::string& filepath) const {
    return filepath.size() > 9 &&
           filepath.substr(filepath.size() - 9) == ".prototxt";
}

bool CaffeProtoxtReader::LoadWeights(const std::string& caffemodel_path) {
    // .caffemodel is a serialized protobuf BinaryProto.
    // We store the path for future weight injection; actual weight mapping
    // requires layer-level weight tensors which are not yet in model_core.
    weights_path_ = caffemodel_path;

    std::ifstream f(caffemodel_path, std::ios::binary);
    if (!f.is_open()) return false;

    // Verify it is not empty (minimal validation).
    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    return size > 0;
}

}  // namespace XER::ModelsBuilder::Reader::Framework
