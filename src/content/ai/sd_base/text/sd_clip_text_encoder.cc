#include "sd_clip_text_encoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _OPENMP
#  include <omp.h>
#endif

namespace XER::AI::SDBase {

namespace {

using Vec = std::vector<float>;

const SdWeightTensor* FindClip(const SdWeightSet& ws, const std::string& name) {
    static const char* kPre[] = {
        "text_model.", "model.text_model.", "text_encoder.text_model.",
        "model.", ""};
    for (const char* p : kPre) {
        if (const auto* t = ws.Find(std::string(p) + name)) return t;
    }
    return nullptr;
}

// LayerNorm over last dim (x: [tokens, dim] row-major)
void LayerNorm(Vec& x, int tokens, int dim,
               const SdWeightTensor* w, const SdWeightTensor* b) {
    const float eps = 1e-5f;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int t = 0; t < tokens; ++t) {
        float* row = x.data() + static_cast<size_t>(t) * dim;
        double mean = 0.0;
        for (int i = 0; i < dim; ++i) mean += row[i];
        mean /= dim;
        double var = 0.0;
        for (int i = 0; i < dim; ++i) {
            const double d = row[i] - mean;
            var += d * d;
        }
        const float inv = static_cast<float>(1.0 / std::sqrt(var / dim + eps));
        for (int i = 0; i < dim; ++i) {
            const float wm = w && i < static_cast<int>(w->data.size())
                                 ? w->data[i] : 1.0f;
            const float bt = b && i < static_cast<int>(b->data.size())
                                 ? b->data[i] : 0.0f;
            row[i] = (row[i] - static_cast<float>(mean)) * inv * wm + bt;
        }
    }
}

// y = W x + b for row-major x [tokens, in], W [out, in]
Vec LinearRows(const Vec& x, int tokens, int in_dim, int out_dim,
               const SdWeightTensor* wt, const SdWeightTensor* bias) {
    Vec y(static_cast<size_t>(tokens) * out_dim, 0.0f);
    const float* Wm = wt->data.data();
    const float* bp = bias ? bias->data.data() : nullptr;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int t = 0; t < tokens; ++t) {
        const float* xr = x.data() + static_cast<size_t>(t) * in_dim;
        float* yr = y.data() + static_cast<size_t>(t) * out_dim;
        for (int o = 0; o < out_dim; ++o) {
            const float* wr = Wm + static_cast<size_t>(o) * in_dim;
            float acc = bp ? bp[o] : 0.0f;
            for (int i = 0; i < in_dim; ++i) acc += wr[i] * xr[i];
            yr[o] = acc;
        }
    }
    return y;
}

// Multi-head self attention over token rows. x: [tokens, dim]
Vec AttentionRows(const Vec& x, int tokens, int dim, int heads,
                  const SdWeightTensor* wq, const SdWeightTensor* wk,
                  const SdWeightTensor* wv, const SdWeightTensor* wo,
                  const SdWeightTensor* bq, const SdWeightTensor* bk,
                  const SdWeightTensor* bv, const SdWeightTensor* bo) {
    Vec q = LinearRows(x, tokens, dim, dim, wq, bq);
    Vec k = LinearRows(x, tokens, dim, dim, wk, bk);
    Vec v = LinearRows(x, tokens, dim, dim, wv, bv);
    const int hd = dim / heads;
    const float scale = 1.0f / std::sqrt(static_cast<float>(hd));
    Vec out(static_cast<size_t>(tokens) * dim, 0.0f);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int t = 0; t < tokens; ++t) {
        for (int h = 0; h < heads; ++h) {
            std::vector<float> scores(static_cast<size_t>(tokens));
            float mx = -1e30f;
            for (int j = 0; j < tokens; ++j) {
                float acc = 0.0f;
                for (int d = 0; d < hd; ++d) {
                    acc += q[static_cast<size_t>(t) * dim + h * hd + d] *
                           k[static_cast<size_t>(j) * dim + h * hd + d];
                }
                acc *= scale;
                scores[j] = acc;
                mx = std::max(mx, acc);
            }
            float sum = 0.0f;
            for (int j = 0; j < tokens; ++j) {
                scores[j] = std::exp(scores[j] - mx);
                sum += scores[j];
            }
            const float inv = 1.0f / std::max(sum, 1e-9f);
            for (int d = 0; d < hd; ++d) {
                float acc = 0.0f;
                for (int j = 0; j < tokens; ++j) {
                    acc += scores[j] *
                           v[static_cast<size_t>(j) * dim + h * hd + d];
                }
                out[static_cast<size_t>(t) * dim + h * hd + d] = acc * inv;
            }
        }
    }
    return LinearRows(out, tokens, dim, dim, wo, bo);
}

}  // namespace

const SdWeightTensor* SdClipTextEncoder::W(const std::string& name) const {
    return FindClip(weights_, name);
}

bool SdClipTextEncoder::Load(const std::string& path,
                             const std::string& tok_dir,
                             std::string* error) {
    std::string werr;
    if (!weights_.Load(path, &werr)) {
        if (error) *error = "clip weights: " + werr;
        return false;
    }
    // derive dims from token_embedding
    if (const auto* te = W("embeddings.token_embedding.weight")) {
        if (te->shape.size() >= 2) {
            ctx_dim_ = static_cast<int>(te->shape[1]);
            layers_ = 0;
            while (W("encoder.layers." + std::to_string(layers_) +
                     ".self_attn.q_proj.weight")) {
                ++layers_;
            }
            heads_ = std::max(1, ctx_dim_ / 64);  // CLIP head_dim = 64
        }
    }
    if (const auto* pe = W("embeddings.position_embedding.weight")) {
        if (pe->shape.size() >= 1) ctx_len_ = static_cast<int>(pe->shape[0]);
    }
    std::string terr;
    if (!tok_.Load(tok_dir, &terr) && error) {
        *error = "clip tokenizer: " + terr;
    }
    return true;
}

std::vector<float> SdClipTextEncoder::Encode(const std::string& prompt) const {
    if (!weights_.IsLoaded() || !tok_.IsLoaded()) return {};

    const int dim = ctx_dim_;
    const int tokens = ctx_len_;
    Vec x(static_cast<size_t>(tokens) * dim, 0.0f);

    const auto* tok_w = W("embeddings.token_embedding.weight");
    const auto* pos_w = W("embeddings.position_embedding.weight");
    if (!tok_w || !pos_w) return {};

    const auto ids = tok_.Encode(prompt, tokens);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int t = 0; t < tokens; ++t) {
        const int64_t id = ids[static_cast<size_t>(t)];
        for (int d = 0; d < dim; ++d) {
            const size_t tw =
                (static_cast<size_t>(id) * dim + d);
            const float tv = tw < tok_w->data.size() ? tok_w->data[tw] : 0.0f;
            const float pv = t < static_cast<int>(pos_w->data.size() / dim)
                                 ? pos_w->data[static_cast<size_t>(t) * dim + d]
                                 : 0.0f;
            x[static_cast<size_t>(t) * dim + d] = tv + pv;
        }
    }

    for (int l = 0; l < layers_; ++l) {
        const std::string p = "encoder.layers." + std::to_string(l) + ".";
        // attn: x + attn(LN1(x))
        Vec h = x;
        LayerNorm(h, tokens, dim, W(p + "layer_norm1.weight"),
                  W(p + "layer_norm1.bias"));
        Vec att = AttentionRows(h, tokens, dim, heads_,
                                W(p + "self_attn.q_proj.weight"),
                                W(p + "self_attn.k_proj.weight"),
                                W(p + "self_attn.v_proj.weight"),
                                W(p + "self_attn.out_proj.weight"),
                                W(p + "self_attn.q_proj.bias"),
                                W(p + "self_attn.k_proj.bias"),
                                W(p + "self_attn.v_proj.bias"),
                                W(p + "self_attn.out_proj.bias"));
        for (size_t i = 0; i < x.size(); ++i) x[i] += att[i];
        // mlp: x + fc2(act(fc1(LN2(x))))
        h = x;
        LayerNorm(h, tokens, dim, W(p + "layer_norm2.weight"),
                  W(p + "layer_norm2.bias"));
        const auto* fc1 = W(p + "mlp.fc1.weight");
        const int mid = fc1 ? static_cast<int>(fc1->shape[0]) : dim * 4;
        Vec m1 = LinearRows(h, tokens, dim, mid, fc1,
                            W(p + "mlp.fc1.bias"));
        // CLIP quick_gelu: x * sigmoid(1.702x)
        for (auto& v : m1) v = v / (1.0f + std::exp(-1.702f * v));
        Vec m2 = LinearRows(m1, tokens, mid, dim,
                            W(p + "mlp.fc2.weight"), W(p + "mlp.fc2.bias"));
        for (size_t i = 0; i < x.size(); ++i) x[i] += m2[i];
    }

    LayerNorm(x, tokens, dim, W("final_layer_norm.weight"),
              W("final_layer_norm.bias"));
    return x;  // [tokens, dim] token-major == EncodeText layout
}

}  // namespace XER::AI::SDBase
