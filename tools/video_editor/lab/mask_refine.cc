// See mask_refine.h — standalone Refine-Edge utilities over w*h
// coverage masks. No decoders, files or UI; nothing here calls into
// object_ops internals.
//
// Implementation notes:
//  * Feather runs two separable passes. The horizontal pass is a
//    straight sliding dot product over contiguous rows and goes
//    through Lab::Sdot (MKL/OpenBLAS when probed, scalar otherwise).
//    The vertical pass gathers each output pixel's column window into
//    a small scratch and likewise feeds Sdot — same math either way,
//    so results are backend-identical up to fp rounding.
//  * Grow is a separable sliding-window extremum (monotone deque,
//    O(w*h) per pass regardless of radius). Max for dilation, min for
//    erosion. Out-of-bounds taps are simply absent from the window,
//    which is equivalent to replicate-border for a min/max filter.
//  * EdgeContrast is element-wise and stays scalar.
#include "mask_refine.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "mkl_accel.h"

namespace XER::VideoEditor::Lab {
namespace {

bool IsBgra(const video::Frame& frame) {
    return frame.IsValid() && frame.Format() == video::PixelFormat::BGRA8;
}

bool ValidMask(std::size_t size, int w, int h) {
    return w > 0 && h > 0 && size == static_cast<std::size_t>(w) * h;
}

// Symmetric Gaussian-approx kernel of half-width r, normalized to
// sum 1. sigma = r/2 keeps the bell inside the window for small radii
// (r=1 -> {0.26, 0.48, 0.26}-ish).
std::vector<float> GaussianKernel(int r) {
    const int n = 2 * r + 1;
    std::vector<float> k(static_cast<std::size_t>(n));
    const float sigma = std::max(static_cast<float>(r) * 0.5f, 0.5f);
    const float inv2s2 = 1.0f / (2.0f * sigma * sigma);
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float d = static_cast<float>(i - r);
        const float v = std::exp(-d * d * inv2s2);
        k[static_cast<std::size_t>(i)] = v;
        sum += v;
    }
    for (float& v : k) v /= sum;
    return k;
}

// dst[x] = <kernel, src[x-r .. x+r]> with replicate-border clamping.
// Interior pixels use Sdot on the contiguous window; the 2r edge
// pixels accumulate tap-by-tap.
void BlurRowKernel(const float* src, float* dst, int n, int r,
                   const float* kernel) {
    const int kn = 2 * r + 1;
    for (int x = 0; x < n; ++x) {
        if (x >= r && x + r < n) {
            dst[x] = Sdot(kn, kernel, src + x - r);
        } else {
            float acc = 0.0f;
            for (int i = 0; i < kn; ++i) {
                const int sx = std::clamp(x + i - r, 0, n - 1);
                acc += kernel[i] * src[sx];
            }
            dst[x] = acc;
        }
    }
}

// dst[y] = <kernel, column src[y-r .. y+r]> (stride `w`), replicate-
// border. Interior outputs gather the column window into `scratch`
// and run it through Sdot.
void BlurColumnKernel(const float* src, float* dst, int n, int w,
                      int r, const float* kernel,
                      std::vector<float>* scratch) {
    const int kn = 2 * r + 1;
    scratch->resize(static_cast<std::size_t>(kn));
    for (int y = 0; y < n; ++y) {
        if (y >= r && y + r < n) {
            for (int i = 0; i < kn; ++i) {
                (*scratch)[static_cast<std::size_t>(i)] =
                    src[static_cast<std::size_t>(y + i - r) * w];
            }
            dst[static_cast<std::size_t>(y) * w] =
                Sdot(kn, kernel, scratch->data());
        } else {
            float acc = 0.0f;
            for (int i = 0; i < kn; ++i) {
                const int sy = std::clamp(y + i - r, 0, n - 1);
                acc += kernel[i] * src[static_cast<std::size_t>(sy) * w];
            }
            dst[static_cast<std::size_t>(y) * w] = acc;
        }
    }
}

// Sliding-window extremum over a 1-D run: out[o] = extremum of
// in[max(0,o-r) .. min(n-1,o+r)]. `prefer` picks the winner: use
// a>=b for max (dilation), a<=b for min (erosion). Monotone deque —
// O(n) regardless of r.
template <typename Prefer>
void WindowExtremum1D(const float* in, float* out, int n, int r,
                      Prefer prefer) {
    std::deque<int> dq;   // candidate indices, best at front
    // Feed positions 0..n-1; window for output o closes once we've
    // consumed index o+r.
    for (int x = 0; x < n; ++x) {
        while (!dq.empty() && !prefer(in[dq.back()], in[x])) {
            dq.pop_back();
        }
        dq.push_back(x);
        const int o = x - r;
        if (o >= 0) {
            while (dq.front() < o - r) dq.pop_front();
            out[o] = in[dq.front()];
        }
    }
    // Outputs whose window right edge clamped at n-1.
    for (int o = std::max(0, n - r); o < n; ++o) {
        while (dq.front() < o - r) dq.pop_front();
        out[o] = in[dq.front()];
    }
}

// Separable square extremum over the whole mask. dilate=true -> max
// (coverage grows), false -> min (coverage erodes).
void Extremum2D(std::vector<float>& mask, int w, int h, int r,
                bool dilate) {
    auto prefer = [dilate](float cur_best, float cand) {
        return dilate ? cur_best >= cand : cur_best <= cand;
    };
    std::vector<float> tmp(mask.size());
    for (int y = 0; y < h; ++y) {
        float* row = mask.data() + static_cast<std::size_t>(y) * w;
        WindowExtremum1D(row, tmp.data() + static_cast<std::size_t>(y) * w,
                         w, r, prefer);
    }
    std::vector<float> col_in(static_cast<std::size_t>(h));
    std::vector<float> col_out(static_cast<std::size_t>(h));
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            col_in[static_cast<std::size_t>(y)] =
                tmp[static_cast<std::size_t>(y) * w + x];
        }
        WindowExtremum1D(col_in.data(), col_out.data(), h, r, prefer);
        for (int y = 0; y < h; ++y) {
            mask[static_cast<std::size_t>(y) * w + x] =
                col_out[static_cast<std::size_t>(y)];
        }
    }
}

std::vector<float> ToFloat(const std::vector<std::uint8_t>& mask) {
    std::vector<float> out(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        out[i] = static_cast<float>(mask[i]) * (1.0f / 255.0f);
    }
    return out;
}

void ToUint8(const std::vector<float>& alpha,
             std::vector<std::uint8_t>* mask) {
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        (*mask)[i] = static_cast<std::uint8_t>(std::lround(
            std::clamp(alpha[i], 0.0f, 1.0f) * 255.0f));
    }
}

}  // namespace

// ------------------------------------------------------------------
// Frame <-> mask
// ------------------------------------------------------------------

std::vector<float> ExtractAlphaMask(const video::Frame& frame) {
    if (!IsBgra(frame)) return {};
    const int w = frame.Width();
    const int h = frame.Height();
    const std::uint8_t* px = frame.Data();
    std::vector<float> alpha(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        alpha[i] = static_cast<float>(px[4 * i + 3]) * (1.0f / 255.0f);
    }
    return alpha;
}

bool ApplyAlphaMask(video::Frame& frame,
                    const std::vector<float>& alpha) {
    if (!IsBgra(frame) ||
        !ValidMask(alpha.size(), frame.Width(), frame.Height())) {
        return false;
    }
    std::uint8_t* px = frame.Data();
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        px[4 * i + 3] = static_cast<std::uint8_t>(std::lround(
            std::clamp(alpha[i], 0.0f, 1.0f) * 255.0f));
    }
    return true;
}

// ------------------------------------------------------------------
// Float masks
// ------------------------------------------------------------------

void FeatherMaskInPlace(std::vector<float>& alpha, int w, int h,
                        float radius_px) {
    const int r = static_cast<int>(std::lround(radius_px));
    if (!ValidMask(alpha.size(), w, h) || r <= 0) return;
    const int radius = std::min(r, 256);
    const std::vector<float> kernel = GaussianKernel(radius);

    // Horizontal pass: contiguous rows, dot-product shaped.
    std::vector<float> tmp(alpha.size());
    for (int y = 0; y < h; ++y) {
        BlurRowKernel(alpha.data() + static_cast<std::size_t>(y) * w,
                      tmp.data() + static_cast<std::size_t>(y) * w,
                      w, radius, kernel.data());
    }
    // Vertical pass: per-column gather + Sdot over the window.
    std::vector<float> scratch;
    for (int x = 0; x < w; ++x) {
        BlurColumnKernel(tmp.data() + x, alpha.data() + x, h, w,
                         radius, kernel.data(), &scratch);
    }
}

void GrowMaskInPlace(std::vector<float>& alpha, int w, int h,
                     float radius_px) {
    const int r = static_cast<int>(std::lround(std::fabs(radius_px)));
    if (!ValidMask(alpha.size(), w, h) || r <= 0) return;
    Extremum2D(alpha, w, h, std::min(r, 512), radius_px > 0.0f);
}

void EdgeContrastInPlace(std::vector<float>& alpha, int w, int h,
                         float contrast) {
    const float c = std::clamp(contrast, -1.0f, 1.0f);
    if (!ValidMask(alpha.size(), w, h) || c == 0.0f) return;
    const float gain = std::pow(10.0f, c);
    for (float& v : alpha) {
        v = std::clamp(0.5f + (v - 0.5f) * gain, 0.0f, 1.0f);
    }
}

void RefineMaskEdgeInPlace(std::vector<float>& alpha, int w, int h,
                           float shift_px, float feather_px,
                           float contrast) {
    GrowMaskInPlace(alpha, w, h, shift_px);
    FeatherMaskInPlace(alpha, w, h, feather_px);
    EdgeContrastInPlace(alpha, w, h, contrast);
}

// ------------------------------------------------------------------
// uint8 masks
// ------------------------------------------------------------------

void FeatherMaskInPlace(std::vector<std::uint8_t>& mask, int w, int h,
                        float radius_px) {
    if (radius_px <= 0.0f) return;
    std::vector<float> alpha = ToFloat(mask);
    FeatherMaskInPlace(alpha, w, h, radius_px);
    ToUint8(alpha, &mask);
}

void GrowMaskInPlace(std::vector<std::uint8_t>& mask, int w, int h,
                     float radius_px) {
    if (radius_px == 0.0f) return;
    std::vector<float> alpha = ToFloat(mask);
    GrowMaskInPlace(alpha, w, h, radius_px);
    ToUint8(alpha, &mask);
}

void EdgeContrastInPlace(std::vector<std::uint8_t>& mask, int w, int h,
                         float contrast) {
    if (contrast == 0.0f) return;
    std::vector<float> alpha = ToFloat(mask);
    EdgeContrastInPlace(alpha, w, h, contrast);
    ToUint8(alpha, &mask);
}

void RefineMaskEdgeInPlace(std::vector<std::uint8_t>& mask, int w,
                           int h, float shift_px, float feather_px,
                           float contrast) {
    GrowMaskInPlace(mask, w, h, shift_px);
    FeatherMaskInPlace(mask, w, h, feather_px);
    EdgeContrastInPlace(mask, w, h, contrast);
}

}  // namespace XER::VideoEditor::Lab
