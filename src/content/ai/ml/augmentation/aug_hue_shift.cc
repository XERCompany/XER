#include "aug_hue_shift.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace XER::ML::Augmentation {

AugHueShift::AugHueShift(float max_shift) : max_shift_(max_shift) {}

void AugHueShift::RgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float cmax = std::max({r, g, b}), cmin = std::min({r, g, b});
    float delta = cmax - cmin;
    v = cmax;
    s = (cmax > 1e-6f) ? (delta / cmax) : 0.0f;
    if (delta < 1e-6f) { h = 0.0f; return; }
    if (cmax == r)      h = 60.0f * std::fmod((g - b) / delta, 6.0f);
    else if (cmax == g) h = 60.0f * ((b - r) / delta + 2.0f);
    else                h = 60.0f * ((r - g) / delta + 4.0f);
    if (h < 0.0f) h += 360.0f;
}

void AugHueShift::HsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s, x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    int sec = static_cast<int>(h / 60.0f) % 6;
    float r1=0,g1=0,b1=0;
    if      (sec==0){r1=c;g1=x;}
    else if (sec==1){r1=x;g1=c;}
    else if (sec==2){g1=c;b1=x;}
    else if (sec==3){g1=x;b1=c;}
    else if (sec==4){r1=x;b1=c;}
    else            {r1=c;b1=x;}
    r = r1+m; g = g1+m; b = b1+m;
}

bool AugHueShift::Apply(ImageSample& sample) {
    if (!enabled_ || sample.channels < 3 || sample.pixels.empty()) return false;
    thread_local std::mt19937 rng{std::random_device{}()};
    float shift = std::uniform_real_distribution<float>(-max_shift_ * 360.0f, max_shift_ * 360.0f)(rng);
    const int W = sample.width, H = sample.height;
    auto& px = sample.pixels;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float r = px[0 * H * W + y * W + x];
            float g = px[1 * H * W + y * W + x];
            float b = px[2 * H * W + y * W + x];
            float hh, s, v;
            RgbToHsv(r, g, b, hh, s, v);
            hh = std::fmod(hh + shift + 360.0f, 360.0f);
            HsvToRgb(hh, s, v, r, g, b);
            px[0 * H * W + y * W + x] = std::clamp(r, 0.0f, 1.0f);
            px[1 * H * W + y * W + x] = std::clamp(g, 0.0f, 1.0f);
            px[2 * H * W + y * W + x] = std::clamp(b, 0.0f, 1.0f);
        }
    }
    return true;
}

}  // namespace XER::ML::Augmentation
