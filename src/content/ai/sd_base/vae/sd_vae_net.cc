#include "sd_vae_net.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _OPENMP
#  include <omp.h>
#endif

namespace XER::AI::SDBase {

namespace {

using Blob = std::vector<float>;  // CHW flat

// VAE weight lookup: try decoder-relative prefixes used by diffusers and
// common checkpoint layouts.
const SdWeightTensor* V(const SdWeightSet& ws, const std::string& name) {
    static const char* kPre[] = {
        "decoder.", "vae.decoder.", "vae.",
        "first_stage_model.decoder.", "model.vae.decoder.", "model.decoder."};
    for (const char* p : kPre) {
        if (const auto* t = ws.Find(p + name)) return t;
    }
    return ws.Find(name);
}

bool HasV(const SdWeightSet& ws, const std::string& name) {
    return V(ws, name) != nullptr;
}

void SiLU(Blob& x) {
    for (auto& v : x) v = v / (1.0f + std::exp(-v));
}

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

Blob Conv2d(const Blob& x, int /*c*/, int h, int w,
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
    const float* bp = bias ? bias->data.data() : nullptr;
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int oc = 0; oc < o; ++oc) {
        const float* w_oc = Wt + static_cast<size_t>(oc) * ic * kh * kw;
        float* y_oc = y.data() + static_cast<size_t>(oc) * oh * ow;
        for (int oy = 0; oy < oh; ++oy) {
            for (int ox = 0; ox < ow; ++ox) {
                float acc = bp ? bp[oc] : 0.0f;
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

Blob AttentionSelf(const Blob& x, int c, int h, int w,
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

    Blob q = LinearSpatial(x, c, h, w, to_q, bq);
    Blob k = LinearSpatial(x, c, h, w, to_k, bk);
    Blob v = LinearSpatial(x, c, h, w, to_v, bv);

    Blob out(static_cast<size_t>(inner) * hw, 0.0f);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int head = 0; head < heads; ++head) {
        const int d0 = head * hd;
        for (int i = 0; i < hw; ++i) {
            std::vector<float> scores(static_cast<size_t>(hw));
            float mx = -1e30f;
            for (int j = 0; j < hw; ++j) {
                float acc = 0.0f;
                for (int d = 0; d < hd; ++d) {
                    acc += q[static_cast<size_t>(d0 + d) * hw + i] *
                           k[static_cast<size_t>(d0 + d) * hw + j];
                }
                acc *= scale;
                scores[j] = acc;
                mx = std::max(mx, acc);
            }
            float sum = 0.0f;
            for (int j = 0; j < hw; ++j) {
                scores[j] = std::exp(scores[j] - mx);
                sum += scores[j];
            }
            const float inv = 1.0f / std::max(sum, 1e-9f);
            for (int d = 0; d < hd; ++d) {
                float acc = 0.0f;
                for (int j = 0; j < hw; ++j) {
                    acc += scores[j] *
                           v[static_cast<size_t>(d0 + d) * hw + j];
                }
                out[static_cast<size_t>(d0 + d) * hw + i] = acc * inv;
            }
        }
    }
    return LinearSpatial(out, inner, h, w, to_out, bo);
}

// VAE ResnetBlock2D: GN->silu->conv1->GN->silu->conv2 + shortcut (plain add)
Blob VaeResnet(const SdWeightSet& ws, const std::string& p,
               const Blob& x, int c, int h, int w) {
    Blob y = x;
    GroupNorm(y, c, h, w, V(ws, p + ".norm1.weight"), V(ws, p + ".norm1.bias"));
    SiLU(y);
    y = Conv2d(y, c, h, w, V(ws, p + ".conv1.weight"), V(ws, p + ".conv1.bias"),
               1, 1);
    const int oc = static_cast<int>(y.size() / (h * w));
    GroupNorm(y, oc, h, w, V(ws, p + ".norm2.weight"), V(ws, p + ".norm2.bias"));
    SiLU(y);
    y = Conv2d(y, oc, h, w, V(ws, p + ".conv2.weight"), V(ws, p + ".conv2.bias"),
               1, 1);

    Blob skip;
    if (const auto* sc = V(ws, p + ".conv_shortcut.weight")) {
        skip = Conv2d(x, c, h, w, sc, V(ws, p + ".conv_shortcut.bias"), 1, 0);
    } else {
        skip = x;
    }
    const size_t n = std::min(y.size(), skip.size());
    for (size_t i = 0; i < n; ++i) y[i] += skip[i];
    return y;
}

// VAE attention block (self-attn only): GN->silu->attention->x+delta.
// Supports both layouts: flat diffusers (to_q/to_k/to_v/proj_attn) and
// transformer_blocks.N.attn1 style.
Blob VaeAttnBlock(const SdWeightSet& ws, const std::string& p,
                  const Blob& x, int c, int h, int w) {
    Blob n = x;
    GroupNorm(n, c, h, w, V(ws, p + ".norm.weight"), V(ws, p + ".norm.bias"));
    SiLU(n);
    const Blob n_in = n;

    auto run_attn = [&](const std::string& a, const SdWeightTensor* out_w,
                        const SdWeightTensor* out_b) {
        Blob y = n;
        Blob att = AttentionSelf(y, c, h, w,
                                 V(ws, a + ".to_q.weight"),
                                 V(ws, a + ".to_k.weight"),
                                 V(ws, a + ".to_v.weight"), out_w,
                                 V(ws, a + ".to_q.bias"), V(ws, a + ".to_k.bias"),
                                 V(ws, a + ".to_v.bias"), out_b);
        for (size_t i = 0; i < y.size(); ++i) y[i] += att[i];
        n = y;
    };

    if (HasV(ws, p + ".to_q.weight")) {
        // flat diffusers AttentionBlock: to_q/k/v + proj_attn
        run_attn(p, V(ws, p + ".proj_attn.weight"),
                 V(ws, p + ".proj_attn.bias"));
    } else {
        for (int j = 0;; ++j) {
            const std::string tb = p + ".transformer_blocks." + std::to_string(j);
            if (!HasV(ws, tb + ".norm1.weight")) break;
            GroupNorm(n, c, h, w, V(ws, tb + ".norm1.weight"),
                      V(ws, tb + ".norm1.bias"));
            SiLU(n);
            const std::string a = tb + ".attn1";
            run_attn(a, V(ws, a + ".to_out.0.weight"),
                     V(ws, a + ".to_out.0.bias"));
        }
    }

    Blob out = x;
    for (size_t i = 0; i < out.size(); ++i) out[i] += n[i] - n_in[i];
    return out;
}

}  // namespace

bool SdVaeNet::Load(const std::string& path, std::string* error) {
    return weights_.Load(path, error);
}

std::vector<float> SdVaeNet::Decode(const float* latent, int h, int w) const {
    if (!weights_.IsLoaded() || !latent || h <= 0 || w <= 0) return {};

    // conv_in: [z,h,w] -> [128,h,w]
    const auto* conv_in = V(weights_, "conv_in.weight");
    if (!conv_in) return {};
    const int zc = static_cast<int>(conv_in->shape[1]);
    Blob cur(latent, latent + static_cast<size_t>(zc) * h * w);
    // undo the SD latent scaling (latents are stored *0.18215)
    for (auto& v : cur) v *= 5.48896f;  // 1/0.18215
    cur = Conv2d(cur, zc, h, w, conv_in, V(weights_, "conv_in.bias"), 1, 1);
    int c = static_cast<int>(cur.size() / (h * w));

    // ---- decoder down_blocks: produce skips, downsample to bottleneck ------
    struct Skip { Blob data; int h, w; };
    std::vector<Skip> skips;
    for (int bi = 0;; ++bi) {
        const std::string bp = "down_blocks." + std::to_string(bi);
        if (!HasV(weights_, bp + ".resnets.0.conv1.weight")) break;
        for (int j = 0;; ++j) {
            const std::string rp = bp + ".resnets." + std::to_string(j);
            if (!HasV(weights_, rp + ".conv1.weight")) break;
            cur = VaeResnet(weights_, rp, cur, c, h, w);
            c = static_cast<int>(cur.size() / (h * w));
            skips.push_back({cur, h, w});
        }
        if (HasV(weights_, bp + ".downsamplers.0.conv.weight")) {
            cur = Conv2d(cur, c, h, w,
                         V(weights_, bp + ".downsamplers.0.conv.weight"),
                         V(weights_, bp + ".downsamplers.0.conv.bias"), 2, 1);
            h /= 2; w /= 2;
            c = static_cast<int>(cur.size() / (h * w));
            skips.push_back({cur, h, w});
        }
    }

    // ---- mid block -----------------------------------------------------------
    if (HasV(weights_, "mid_block.resnets.0.conv1.weight")) {
        cur = VaeResnet(weights_, "mid_block.resnets.0", cur, c, h, w);
        c = static_cast<int>(cur.size() / (h * w));
    }
    for (int k = 0;; ++k) {
        const std::string ap = "mid_block.attentions." + std::to_string(k);
        if (!HasV(weights_, ap + ".norm.weight")) break;
        cur = VaeAttnBlock(weights_, ap, cur, c, h, w);
        c = static_cast<int>(cur.size() / (h * w));
    }
    if (HasV(weights_, "mid_block.resnets.1.conv1.weight")) {
        cur = VaeResnet(weights_, "mid_block.resnets.1", cur, c, h, w);
        c = static_cast<int>(cur.size() / (h * w));
    }

    // ---- up blocks -------------------------------------------------------------
    size_t skip_idx = skips.size();
    for (int bi = 0;; ++bi) {
        const std::string bp = "up_blocks." + std::to_string(bi);
        if (!HasV(weights_, bp + ".resnets.0.conv1.weight")) break;
        int res_count = 0;
        while (HasV(weights_, bp + ".resnets." + std::to_string(res_count) +
                                ".conv1.weight")) {
            ++res_count;
        }
        if (res_count == 0 || skip_idx < static_cast<size_t>(res_count)) break;
        for (int j = 0; j < res_count; ++j) {
            --skip_idx;
            const Skip& sk = skips[skip_idx];
            Blob cat = CatChannels(cur, sk.data, h, w);
            cur = VaeResnet(weights_,
                            bp + ".resnets." + std::to_string(j),
                            cat, c + static_cast<int>(sk.data.size() /
                                                      (sk.h * sk.w)),
                            h, w);
            c = static_cast<int>(cur.size() / (h * w));
            const std::string ap = bp + ".attentions." + std::to_string(j);
            if (HasV(weights_, ap + ".norm.weight")) {
                cur = VaeAttnBlock(weights_, ap, cur, c, h, w);
                c = static_cast<int>(cur.size() / (h * w));
            }
        }
        if (HasV(weights_, bp + ".upsamplers.0.conv.weight")) {
            cur = Upsample2x(cur, c, h, w);
            h *= 2; w *= 2;
            cur = Conv2d(cur, c, h, w,
                         V(weights_, bp + ".upsamplers.0.conv.weight"),
                         V(weights_, bp + ".upsamplers.0.conv.bias"), 1, 1);
            c = static_cast<int>(cur.size() / (h * w));
        }
    }

    // ---- output head -------------------------------------------------------------
    GroupNorm(cur, c, h, w, V(weights_, "conv_norm_out.weight"),
              V(weights_, "conv_norm_out.bias"));
    SiLU(cur);
    cur = Conv2d(cur, c, h, w, V(weights_, "conv_out.weight"),
                 V(weights_, "conv_out.bias"), 1, 1);
    return cur;  // [3, H, W]
}

}  // namespace XER::AI::SDBase
