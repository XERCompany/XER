#include "sd_unet.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _OPENMP
#  include <omp.h>
#endif

namespace XER::AI::SDBase {

namespace {

using Blob = std::vector<float>;  // CHW flat: [C, H, W]

// ---- weight lookup -------------------------------------------------------

const SdWeightTensor* W(const SdWeightSet& ws, const std::string& name) {
    return ws.FindModel(name);
}

bool Has(const SdWeightSet& ws, const std::string& name) {
    return W(ws, name) != nullptr;
}

// ---- elementwise ops -----------------------------------------------------

void SiLU(Blob& x) {
    for (auto& v : x) v = v / (1.0f + std::exp(-v));
}

void Gelu(Blob& x) {
    for (auto& v : x) {
        const float u = 0.7978845608f * (v + 0.044715f * v * v * v);
        v = 0.5f * v * (1.0f + std::tanh(u));
    }
}

// ---- GroupNorm (num_groups=32, affine per channel) ------------------------
void GroupNorm(Blob& x, int c, int h, int w,
               const SdWeightTensor* gamma, const SdWeightTensor* beta,
               int groups = 32) {
    if (c <= 0 || h <= 0 || w <= 0) return;
    groups = std::min(groups, c);
    const int cg = c / groups;
    const int hw = h * w;
    const float eps = 1e-5f;

    for (int g = 0; g < groups; ++g) {
        const int c0 = g * cg;
        const size_t count = static_cast<size_t>(cg) * hw;
        double sum = 0.0;
        for (int cc = 0; cc < cg; ++cc) {
            const float* p = x.data() + static_cast<size_t>(c0 + cc) * hw;
            for (int i = 0; i < hw; ++i) sum += p[i];
        }
        const double mean = sum / static_cast<double>(count);
        double var = 0.0;
        for (int cc = 0; cc < cg; ++cc) {
            const float* p = x.data() + static_cast<size_t>(c0 + cc) * hw;
            for (int i = 0; i < hw; ++i) {
                const double d = p[i] - mean;
                var += d * d;
            }
        }
        const float inv = static_cast<float>(
            1.0 / std::sqrt(var / static_cast<double>(count) + eps));
        for (int cc = 0; cc < cg; ++cc) {
            const int ch = c0 + cc;
            float* p = x.data() + static_cast<size_t>(ch) * hw;
            const float gm = gamma && ch < static_cast<int>(gamma->data.size())
                                 ? gamma->data[ch] : 1.0f;
            const float bt = beta && ch < static_cast<int>(beta->data.size())
                                 ? beta->data[ch] : 0.0f;
            for (int i = 0; i < hw; ++i) {
                p[i] = (p[i] - static_cast<float>(mean)) * inv * gm + bt;
            }
        }
    }
}

// ---- dense linear: y[o] = sum_i W[o,i]*x[i] + b[o] -------------------------
Blob Linear(const Blob& x, int in_dim, int out_dim,
            const SdWeightTensor* wt, const SdWeightTensor* bias) {
    Blob y(static_cast<size_t>(out_dim), 0.0f);
    const float* Wm = wt->data.data();
    const float* b = bias ? bias->data.data() : nullptr;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int o = 0; o < out_dim; ++o) {
        const float* wr = Wm + static_cast<size_t>(o) * in_dim;
        float acc = b ? b[o] : 0.0f;
        for (int i = 0; i < in_dim; ++i) acc += wr[i] * x[i];
        y[o] = acc;
    }
    return y;
}

// ---- conv2d: x[C,H,W], wt[O,I,kh,kw], optional bias[O] ---------------------
Blob Conv2d(const Blob& x, int c, int h, int w,
            const SdWeightTensor* wt, const SdWeightTensor* bias,
            int stride = 1, int pad = 1) {
    const int o = static_cast<int>(wt->shape[0]);
    const int ic = static_cast<int>(wt->shape[1]);
    const int kh = static_cast<int>(wt->shape.size() > 2 ? wt->shape[2] : 1);
    const int kw = static_cast<int>(wt->shape.size() > 3 ? wt->shape[3] : 1);
    const int oh = (h + 2 * pad - kh) / stride + 1;
    const int ow = (w + 2 * pad - kw) / stride + 1;
    Blob y(static_cast<size_t>(o) * oh * ow, 0.0f);

    const float* Wt = wt->data.data();
    const float* bias_p = bias ? bias->data.data() : nullptr;

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int oc = 0; oc < o; ++oc) {
        const float* w_oc = Wt + static_cast<size_t>(oc) * ic * kh * kw;
        float* y_oc = y.data() + static_cast<size_t>(oc) * oh * ow;
        for (int oy = 0; oy < oh; ++oy) {
            for (int ox = 0; ox < ow; ++ox) {
                float acc = bias_p ? bias_p[oc] : 0.0f;
                for (int icc = 0; icc < ic; ++icc) {
                    const float* x_ic = x.data() + static_cast<size_t>(icc) * h * w;
                    const float* w_ic = w_oc + static_cast<size_t>(icc) * kh * kw;
                    for (int ky = 0; ky < kh; ++ky) {
                        const int iy = oy * stride + ky - pad;
                        if (iy < 0 || iy >= h) continue;
                        for (int kx = 0; kx < kw; ++kx) {
                            const int ix = ox * stride + kx - pad;
                            if (ix < 0 || ix >= w) continue;
                            acc += x_ic[iy * w + ix] * w_ic[ky * kw + kx];
                        }
                    }
                }
                y_oc[oy * ow + ox] = acc;
            }
        }
    }
    return y;
}

// 1x1 conv == per-position linear over channel dim.
Blob LinearSpatial(const Blob& x, int c, int h, int w,
                   const SdWeightTensor* wt, const SdWeightTensor* bias) {
    const int o = static_cast<int>(wt->shape[0]);
    const int hw = h * w;
    Blob y(static_cast<size_t>(o) * hw, 0.0f);
    const float* Wm = wt->data.data();
    const float* bp = bias ? bias->data.data() : nullptr;

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int pos = 0; pos < hw; ++pos) {
        for (int oc = 0; oc < o; ++oc) {
            const float* wr = Wm + static_cast<size_t>(oc) * c;
            float acc = bp ? bp[oc] : 0.0f;
            for (int ic = 0; ic < c; ++ic) {
                acc += wr[ic] * x[static_cast<size_t>(ic) * hw + pos];
            }
            y[static_cast<size_t>(oc) * hw + pos] = acc;
        }
    }
    return y;
}

Blob CatChannels(const Blob& a, const Blob& b, int h, int w) {
    const int ca = static_cast<int>(a.size() / (h * w));
    const int cb = static_cast<int>(b.size() / (h * w));
    Blob out(static_cast<size_t>(ca + cb) * h * w);
    const size_t plane = static_cast<size_t>(h) * w;
    std::memcpy(out.data(), a.data(), plane * ca * sizeof(float));
    std::memcpy(out.data() + plane * ca, b.data(), plane * cb * sizeof(float));
    return out;
}

Blob Upsample2x(const Blob& x, int c, int h, int w) {
    const int nh = h * 2, nw = w * 2;
    Blob out(static_cast<size_t>(c) * nh * nw);
    for (int ch = 0; ch < c; ++ch) {
        const float* src = x.data() + static_cast<size_t>(ch) * h * w;
        float* dst = out.data() + static_cast<size_t>(ch) * nh * nw;
        for (int y = 0; y < nh; ++y) {
            const int sy = y / 2;
            for (int xx = 0; xx < nw; ++xx) {
                dst[y * nw + xx] = src[sy * w + (xx / 2)];
            }
        }
    }
    return out;
}

// ---- attention ------------------------------------------------------------
// x: [C, HW]; kv==nullptr -> self-attention, else kv is [kv_c, kv_len] tokens.
Blob Attention(const Blob& x, int c, int h, int w,
               const Blob* kv, int kv_c, int kv_len,
               const SdWeightTensor* to_q, const SdWeightTensor* to_k,
               const SdWeightTensor* to_v, const SdWeightTensor* to_out,
               const SdWeightTensor* bq, const SdWeightTensor* bk,
               const SdWeightTensor* bv, const SdWeightTensor* bo) {
    const int hw = h * w;
    const int inner = static_cast<int>(to_q->shape[0]);
    int heads = inner / 64;
    if (heads < 1) heads = 1;
    const int hd = inner / heads;
    const float scale = 1.0f / std::sqrt(static_cast<float>(hd));
    const int kv_hw = kv ? kv_len : hw;

    Blob q = LinearSpatial(x, c, h, w, to_q, bq);            // [inner, hw]
    Blob k, v;
    if (kv) {
        k = LinearSpatial(*kv, kv_c, 1, kv_len, to_k, bk);   // [inner, kv_len]
        v = LinearSpatial(*kv, kv_c, 1, kv_len, to_v, bv);
    } else {
        k = LinearSpatial(x, c, h, w, to_k, bk);
        v = LinearSpatial(x, c, h, w, to_v, bv);
    }

    Blob out(static_cast<size_t>(inner) * hw, 0.0f);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int head = 0; head < heads; ++head) {
        const int d0 = head * hd;
        for (int i = 0; i < hw; ++i) {
            std::vector<float> scores(static_cast<size_t>(kv_hw));
            float mx = -1e30f;
            for (int j = 0; j < kv_hw; ++j) {
                float acc = 0.0f;
                for (int d = 0; d < hd; ++d) {
                    acc += q[static_cast<size_t>(d0 + d) * hw + i] *
                           k[static_cast<size_t>(d0 + d) * kv_hw + j];
                }
                acc *= scale;
                scores[j] = acc;
                mx = std::max(mx, acc);
            }
            float sum = 0.0f;
            for (int j = 0; j < kv_hw; ++j) {
                scores[j] = std::exp(scores[j] - mx);
                sum += scores[j];
            }
            const float inv = 1.0f / std::max(sum, 1e-9f);
            for (int d = 0; d < hd; ++d) {
                float acc = 0.0f;
                for (int j = 0; j < kv_hw; ++j) {
                    acc += scores[j] *
                           v[static_cast<size_t>(d0 + d) * kv_hw + j];
                }
                out[static_cast<size_t>(d0 + d) * hw + i] = acc * inv;
            }
        }
    }
    return LinearSpatial(out, inner, h, w, to_out, bo);
}

// ---- UNet blocks -----------------------------------------------------------

Blob ResNet(const SdWeightSet& ws, const std::string& p,
            const Blob& x, int c, int h, int w,
            const Blob& temb) {
    Blob y = x;
    GroupNorm(y, c, h, w, W(ws, p + ".norm1.weight"),
              W(ws, p + ".norm1.bias"));
    SiLU(y);
    y = Conv2d(y, c, h, w, W(ws, p + ".conv1.weight"),
               W(ws, p + ".conv1.bias"), 1, 1);
    const int oc = static_cast<int>(y.size() / (h * w));
    const auto* proj = W(ws, p + ".time_emb_proj.weight");
    if (proj) {
        const auto* pb = W(ws, p + ".time_emb_proj.bias");
        Blob t = Linear(temb, static_cast<int>(temb.size()), oc, proj, pb);
        for (int ch = 0; ch < oc; ++ch) {
            const float tv = t[ch];
            float* plane = y.data() + static_cast<size_t>(ch) * h * w;
            for (int i = 0; i < h * w; ++i) plane[i] += tv;
        }
    }
    GroupNorm(y, oc, h, w, W(ws, p + ".norm2.weight"),
              W(ws, p + ".norm2.bias"));
    SiLU(y);
    y = Conv2d(y, oc, h, w, W(ws, p + ".conv2.weight"),
               W(ws, p + ".conv2.bias"), 1, 1);

    Blob skip;
    const auto* sc = W(ws, p + ".conv_shortcut.weight");
    const auto* scb = W(ws, p + ".conv_shortcut.bias");
    if (sc) {
        skip = Conv2d(x, c, h, w, sc, scb, 1, 0);
    } else {
        skip = x;
    }
    const float k = 0.70710678f;
    for (size_t i = 0; i < y.size(); ++i) y[i] = (y[i] + skip[i]) * k;
    return y;
}

// BasicTransformerBlock: residual-accumulated attn1/attn2/ff sub-blocks.
Blob TransformerBlock(const SdWeightSet& ws, const std::string& p,
                      const Blob& x, int c, int h, int w,
                      const Blob& ctx, int ctx_len, int ctx_dim) {
    Blob y = x;

    {
        Blob n = y;
        GroupNorm(n, c, h, w, W(ws, p + ".norm1.weight"),
                  W(ws, p + ".norm1.bias"));
        SiLU(n);
        const std::string a = p + ".attn1";
        Blob att = Attention(n, c, h, w, nullptr, 0, 0,
                             W(ws, a + ".to_q.weight"), W(ws, a + ".to_k.weight"),
                             W(ws, a + ".to_v.weight"),
                             W(ws, a + ".to_out.0.weight"),
                             W(ws, a + ".to_q.bias"), W(ws, a + ".to_k.bias"),
                             W(ws, a + ".to_v.bias"), W(ws, a + ".to_out.0.bias"));
        for (size_t i = 0; i < y.size(); ++i) y[i] += att[i];
    }
    {
        Blob n = y;
        GroupNorm(n, c, h, w, W(ws, p + ".norm2.weight"),
                  W(ws, p + ".norm2.bias"));
        SiLU(n);
        const std::string a = p + ".attn2";
        Blob att = Attention(n, c, h, w, &ctx, ctx_dim, ctx_len,
                             W(ws, a + ".to_q.weight"), W(ws, a + ".to_k.weight"),
                             W(ws, a + ".to_v.weight"),
                             W(ws, a + ".to_out.0.weight"),
                             W(ws, a + ".to_q.bias"), W(ws, a + ".to_k.bias"),
                             W(ws, a + ".to_v.bias"), W(ws, a + ".to_out.0.bias"));
        for (size_t i = 0; i < y.size(); ++i) y[i] += att[i];
    }
    {
        Blob n = y;
        GroupNorm(n, c, h, w, W(ws, p + ".norm3.weight"),
                  W(ws, p + ".norm3.bias"));
        SiLU(n);
        const std::string f = p + ".ff.net";
        Blob ff;
        if (Has(ws, f + ".0.proj.weight")) {
            // GEGLU: proj -> [2*inner, C]; gelu(left)*right -> net.2
            const int o = static_cast<int>(W(ws, f + ".0.proj.weight")->shape[0]);
            const int inner = o / 2;
            const int hw = h * w;
            Blob proj_out = LinearSpatial(n, c, h, w,
                                          W(ws, f + ".0.proj.weight"),
                                          W(ws, f + ".0.proj.bias"));
            Blob gated(static_cast<size_t>(inner) * hw, 0.0f);
            for (int pos = 0; pos < hw; ++pos) {
                for (int d = 0; d < inner; ++d) {
                    const float g = proj_out[static_cast<size_t>(d) * hw + pos];
                    const float gate = 0.5f * g *
                        (1.0f + std::tanh(0.7978845608f *
                            (g + 0.044715f * g * g * g)));
                    gated[static_cast<size_t>(d) * hw + pos] =
                        gate * proj_out[static_cast<size_t>(inner + d) * hw + pos];
                }
            }
            ff = LinearSpatial(gated, inner, h, w, W(ws, f + ".2.weight"),
                               W(ws, f + ".2.bias"));
        } else {
            Blob h1 = LinearSpatial(n, c, h, w,
                                    W(ws, p + ".ff.linear_1.weight"),
                                    W(ws, p + ".ff.linear_1.bias"));
            Gelu(h1);
            ff = LinearSpatial(h1, static_cast<int>(h1.size() / (h * w)), h, w,
                               W(ws, p + ".ff.linear_2.weight"),
                               W(ws, p + ".ff.linear_2.bias"));
        }
        for (size_t i = 0; i < y.size(); ++i) y[i] += ff[i];
    }
    return y;
}

// Attention wrapper: GN->silu->transformer blocks; result = x + delta where
// delta = transformer(norm(x)) - norm(x)  (true outer residual).
Blob AttnBlock(const SdWeightSet& ws, const std::string& p,
               const Blob& x, int c, int h, int w,
               const Blob& ctx, int ctx_len, int ctx_dim) {
    Blob n = x;
    GroupNorm(n, c, h, w, W(ws, p + ".norm.weight"), W(ws, p + ".norm.bias"));
    SiLU(n);
    const Blob n_in = n;
    for (int j = 0;; ++j) {
        const std::string tb =
            p + ".transformer_blocks." + std::to_string(j);
        if (!Has(ws, tb + ".norm1.weight")) break;
        n = TransformerBlock(ws, tb, n, c, h, w, ctx, ctx_len, ctx_dim);
    }
    Blob out = x;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] += n[i] - n_in[i];
    }
    return out;
}

// ---- sinusoidal timestep embedding ----------------------------------------
Blob TimestepEmbedding(float t, int dim) {
    const int half = dim / 2;
    Blob emb(static_cast<size_t>(dim));
    const float s = t / 1000.0f;
    for (int i = 0; i < half; ++i) {
        const float f = std::exp(-std::log(10000.0f) *
                                 static_cast<float>(i) /
                                 static_cast<float>(half));
        emb[i] = std::sin(s * f);
        emb[i + half] = std::cos(s * f);
    }
    return emb;
}

}  // namespace

bool SdUnet::Load(const std::string& path, std::string* error) {
    return weights_.Load(path, error);
}

void SdUnet::Forward(const float* x, int h, int w,
                     const float* ctx, int ctx_len, int ctx_dim,
                     float t, float* out_eps) const {
    if (!weights_.IsLoaded() || !x || !out_eps || h <= 0 || w <= 0) return;

    // ---- time embedding -----------------------------------------------------
    const auto* te0 = W(weights_, "time_embed.0.weight");
    const auto* te2 = W(weights_, "time_embed.2.weight");
    const int te_in = te0 ? static_cast<int>(te0->shape[1]) : 320;
    const int te_mid = te0 ? static_cast<int>(te0->shape[0]) : te_in;
    const int te_out = te2 ? static_cast<int>(te2->shape[0]) : te_mid;

    Blob temb = TimestepEmbedding(t, te_in);
    if (te0) {
        temb = Linear(temb, te_in, te_mid, te0,
                      W(weights_, "time_embed.0.bias"));
        SiLU(temb);
    }
    if (te2) {
        temb = Linear(temb, te_mid, te_out, te2,
                      W(weights_, "time_embed.2.bias"));
    }

    // ---- context tokens -> [ctx_dim, ctx_len] -------------------------------
    int ctx_c = ctx_dim > 0 ? ctx_dim : 768;
    int ctx_tokens = ctx_len > 0 ? ctx_len : 1;
    Blob ctx_blob(static_cast<size_t>(ctx_c) * ctx_tokens, 0.0f);
    if (ctx && ctx_dim > 0) {
        for (int tok = 0; tok < ctx_tokens; ++tok)
            for (int d = 0; d < ctx_c; ++d)
                ctx_blob[static_cast<size_t>(d) * ctx_tokens + tok] =
                    ctx[tok * ctx_dim + d];
    }

    // ---- conv_in -------------------------------------------------------------
    const auto* conv_in = W(weights_, "conv_in.weight");
    const int cin = conv_in ? static_cast<int>(conv_in->shape[1]) : 4;
    Blob cur(x, x + static_cast<size_t>(cin) * h * w);
    cur = Conv2d(cur, cin, h, w, conv_in,
                 W(weights_, "conv_in.bias"), 1, 1);
    int c = static_cast<int>(cur.size() / (h * w));

    // ---- down blocks ---------------------------------------------------------
    struct Skip { Blob data; int h, w; };
    std::vector<Skip> skips;
    auto push_skip = [&](const Blob& b, int hh, int ww) {
        skips.push_back({b, hh, ww});
    };

    for (int bi = 0;; ++bi) {
        const std::string bp = "down_blocks." + std::to_string(bi);
        if (!Has(weights_, bp + ".resnets.0.conv1.weight")) break;
        for (int j = 0;; ++j) {
            const std::string rp = bp + ".resnets." + std::to_string(j);
            if (!Has(weights_, rp + ".conv1.weight")) break;
            cur = ResNet(weights_, rp, cur, c, h, w, temb);
            c = static_cast<int>(cur.size() / (h * w));
            for (int k = 0;; ++k) {
                const std::string ap = bp + ".attentions." + std::to_string(k);
                if (!Has(weights_, ap + ".norm.weight")) break;
                cur = AttnBlock(weights_, ap, cur, c, h, w,
                                ctx_blob, ctx_tokens, ctx_c);
                c = static_cast<int>(cur.size() / (h * w));
            }
            push_skip(cur, h, w);
        }
        if (Has(weights_, bp + ".downsamplers.0.conv.weight")) {
            cur = Conv2d(cur, c, h, w,
                         W(weights_, bp + ".downsamplers.0.conv.weight"),
                         W(weights_, bp + ".downsamplers.0.conv.bias"),
                         2, 1);
            h /= 2; w /= 2;
            c = static_cast<int>(cur.size() / (h * w));
            push_skip(cur, h, w);
        }
    }

    // ---- mid block: resnets.0 -> attentions -> resnets.1.. ------------------
    if (Has(weights_, "mid_block.resnets.0.conv1.weight")) {
        cur = ResNet(weights_, "mid_block.resnets.0", cur, c, h, w, temb);
        c = static_cast<int>(cur.size() / (h * w));
    }
    for (int k = 0;; ++k) {
        const std::string ap = "mid_block.attentions." + std::to_string(k);
        if (!Has(weights_, ap + ".norm.weight")) break;
        cur = AttnBlock(weights_, ap, cur, c, h, w,
                        ctx_blob, ctx_tokens, ctx_c);
        c = static_cast<int>(cur.size() / (h * w));
    }
    for (int j = 1;; ++j) {
        const std::string rp = "mid_block.resnets." + std::to_string(j);
        if (!Has(weights_, rp + ".conv1.weight")) break;
        cur = ResNet(weights_, rp, cur, c, h, w, temb);
        c = static_cast<int>(cur.size() / (h * w));
    }

    // ---- up blocks -----------------------------------------------------------
    size_t skip_idx = skips.size();
    for (int bi = 0;; ++bi) {
        const std::string bp = "up_blocks." + std::to_string(bi);
        if (!Has(weights_, bp + ".resnets.0.conv1.weight")) break;

        int res_count = 0;
        while (Has(weights_, bp + ".resnets." + std::to_string(res_count) +
                               ".conv1.weight")) {
            ++res_count;
        }
        if (res_count == 0 ||
            skip_idx < static_cast<size_t>(res_count)) break;

        for (int j = 0; j < res_count; ++j) {
            --skip_idx;
            const Skip& sk = skips[skip_idx];
            Blob cat = CatChannels(cur, sk.data, h, w);
            cur = ResNet(weights_,
                         bp + ".resnets." + std::to_string(j),
                         cat, c + static_cast<int>(sk.data.size() /
                                                   (sk.h * sk.w)),
                         h, w, temb);
            c = static_cast<int>(cur.size() / (h * w));
            const std::string ap = bp + ".attentions." + std::to_string(j);
            if (Has(weights_, ap + ".norm.weight")) {
                cur = AttnBlock(weights_, ap, cur, c, h, w,
                                ctx_blob, ctx_tokens, ctx_c);
                c = static_cast<int>(cur.size() / (h * w));
            }
        }

        if (Has(weights_, bp + ".upsamplers.0.conv.weight")) {
            cur = Upsample2x(cur, c, h, w);
            h *= 2; w *= 2;
            cur = Conv2d(cur, c, h, w,
                         W(weights_, bp + ".upsamplers.0.conv.weight"),
                         W(weights_, bp + ".upsamplers.0.conv.bias"),
                         1, 1);
            c = static_cast<int>(cur.size() / (h * w));
        }
    }

    // ---- output head ---------------------------------------------------------
    GroupNorm(cur, c, h, w, W(weights_, "conv_norm_out.weight"),
              W(weights_, "conv_norm_out.bias"));
    SiLU(cur);
    cur = Conv2d(cur, c, h, w, W(weights_, "conv_out.weight"),
                 W(weights_, "conv_out.bias"), 1, 1);

    const int oc = static_cast<int>(cur.size() / (h * w));
    const size_t need = static_cast<size_t>(oc) * h * w;
    const size_t n = std::min(need, cur.size());
    std::memcpy(out_eps, cur.data(), n * sizeof(float));
    if (need > cur.size()) {
        std::fill(out_eps + cur.size(), out_eps + need, 0.0f);
    }
}

}  // namespace XER::AI::SDBase
