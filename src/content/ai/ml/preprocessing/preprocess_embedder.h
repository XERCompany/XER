#pragma once
#include <vector>
#include <unordered_map>
#include <cstddef>

namespace XER::ML::Preprocessing {

/// Look up fixed-size float embeddings for integer token IDs
class PreprocessEmbedder {
public:
    /// Load embedding table: rows = vocab, cols = embedding_dim
    void LoadTable(const std::vector<std::vector<float>>& table);

    /// Embed a sequence of token IDs → matrix (n_tokens × embed_dim)
    std::vector<std::vector<float>> Embed(const std::vector<int>& token_ids) const;

    bool IsFitted() const { return !table_.empty(); }
    size_t EmbedDim() const { return embed_dim_; }

private:
    std::vector<std::vector<float>> table_;
    size_t embed_dim_ = 0;
};

}  // namespace XER::ML::Preprocessing
