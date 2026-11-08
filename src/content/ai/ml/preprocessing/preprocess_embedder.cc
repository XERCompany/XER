#include "preprocess_embedder.h"

namespace XER::ML::Preprocessing {

void PreprocessEmbedder::LoadTable(const std::vector<std::vector<float>>& table) {
    table_ = table;
    embed_dim_ = table.empty() ? 0 : table[0].size();
}

std::vector<std::vector<float>> PreprocessEmbedder::Embed(
        const std::vector<int>& token_ids) const {
    std::vector<std::vector<float>> out;
    out.reserve(token_ids.size());
    for (int id : token_ids) {
        if (id >= 0 && static_cast<size_t>(id) < table_.size())
            out.push_back(table_[id]);
        else
            out.push_back(std::vector<float>(embed_dim_, 0.0f));
    }
    return out;
}

}  // namespace XER::ML::Preprocessing
