// Impl file — halftone / emboss_color / edge_glow / tone_split.
#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx {
namespace {

inline float luma(const uint8_t* p) {
  return 0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2];
}

inline uint8_t to8(float v) {
  return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}

inline void hue_rgb(float deg, float& r, float& g, float& b) {
  float h = std::fmod(deg, 360.0f);
  if (h < 0.0f) h += 360.0f;
  h /= 60.0f;
  const float x = 1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f);
  float rr = 0.0f, gg = 0.0f, bb = 0.0f;
  switch (static_cast<int>(h)) {
    case 0: rr = 1.0f; gg = x; break;
    case 1: rr = x; gg = 1.0f; break;
    case 2: gg = 1.0f; bb = x; break;
    case 3: gg = x; bb = 1.0f; break;
    case 4: rr = x; bb = 1.0f; break;
    default: rr = 1.0f; bb = x; break;
  }
  r = rr * 255.0f;
  g = gg * 255.0f;
  b = bb * 255.0f;
}

inline bool valid(const XER_VideoFrame* f) {
  return f && f->data && f->width > 0 && f->height > 0;
}

}  // namespace

int halftone(XER_VideoFrame* f, const float* p, int32_t n) {
  if (!valid(f)) return 1;
  const int cs = std::clamp(n > 0 ? static_cast<int>(p[0] + 0.5f) : 8, 4, 32);
  const int w = f->width, h = f->height, st = f->stride;
  const float half = cs * 0.5f;
  for (int cy = 0; cy < h; cy += cs) {
    const int y1 = std::min(cy + cs, h);
    for (int cx = 0; cx < w; cx += cs) {
      const int x1 = std::min(cx + cs, w);
      double sum = 0.0;
      for (int y = cy; y < y1; ++y) {
        const uint8_t* row = f->data + y * st;
        for (int x = cx; x < x1; ++x) sum += luma(row + x * 4);
      }
      const float avg =
          static_cast<float>(sum / ((y1 - cy) * (x1 - cx)));
      const float r = half * (1.0f - avg / 255.0f);
      const float cxc = cx + (x1 - cx) * 0.5f;
      const float cyc = cy + (y1 - cy) * 0.5f;
      for (int y = cy; y < y1; ++y) {
        uint8_t* row = f->data + y * st;
        for (int x = cx; x < x1; ++x) {
          const float dx = x + 0.5f - cxc, dy = y + 0.5f - cyc;
          const uint8_t v = dx * dx + dy * dy <= r * r ? 0 : 255;
          uint8_t* px = row + x * 4;
          px[0] = px[1] = px[2] = v;
        }
      }
    }
  }
  return 0;
}

int emboss_color(XER_VideoFrame* f, const float* p, int32_t n) {
  if (!valid(f)) return 1;
  const float rad =
      (n > 0 ? p[0] : 135.0f) * (3.14159265f / 180.0f);
  const float s = n > 1 ? p[1] : 1.0f;
  const int dx = static_cast<int>(std::lround(std::cos(rad)));
  const int dy = static_cast<int>(std::lround(std::sin(rad)));
  const int w = f->width, h = f->height, st = f->stride;
  const std::vector<uint8_t> src(f->data, f->data + st * h);
  for (int y = 0; y < h; ++y) {
    uint8_t* row = f->data + y * st;
    const int ny = std::clamp(y + dy, 0, h - 1);
    const uint8_t* srow = src.data() + ny * st;
    const uint8_t* crow = src.data() + y * st;
    for (int x = 0; x < w; ++x) {
      const int nx = std::clamp(x + dx, 0, w - 1);
      const uint8_t* nb = srow + nx * 4;
      const uint8_t* cur = crow + x * 4;
      uint8_t* px = row + x * 4;
      for (int c = 0; c < 3; ++c)
        px[c] = to8(128.0f + s * (static_cast<float>(cur[c]) - nb[c]));
    }
  }
  return 0;
}

int edge_glow(XER_VideoFrame* f, const float* p, int32_t n) {
  if (!valid(f)) return 1;
  const float th = n > 0 ? p[0] : 40.0f;
  const float k = n > 1 ? p[1] : 1.5f;
  const int w = f->width, h = f->height, st = f->stride;
  std::vector<float> gray(w * h);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = f->data + y * st;
    for (int x = 0; x < w; ++x) gray[y * w + x] = luma(row + x * 4);
  }
  const auto g = [&](int x, int y) {
    return gray[std::clamp(y, 0, h - 1) * w +
                std::clamp(x, 0, w - 1)];
  };
  for (int y = 0; y < h; ++y) {
    uint8_t* row = f->data + y * st;
    for (int x = 0; x < w; ++x) {
      const float gx = g(x + 1, y - 1) + 2 * g(x + 1, y) + g(x + 1, y + 1) -
                       g(x - 1, y - 1) - 2 * g(x - 1, y) - g(x - 1, y + 1);
      const float gy = g(x - 1, y + 1) + 2 * g(x, y + 1) + g(x + 1, y + 1) -
                       g(x - 1, y - 1) - 2 * g(x, y - 1) - g(x + 1, y - 1);
      const float mag = std::sqrt(gx * gx + gy * gy) * 0.25f;
      if (mag > th) {
        const float add = mag * k;
        uint8_t* px = row + x * 4;
        px[0] = to8(px[0] + add);
        px[1] = to8(px[1] + add);
        px[2] = to8(px[2] + add);
      }
    }
  }
  return 0;
}

int tone_split(XER_VideoFrame* f, const float* p, int32_t n) {
  if (!valid(f)) return 1;
  float sr, sg, sb, hr, hg, hb;
  hue_rgb(n > 0 ? p[0] : 220.0f, sr, sg, sb);
  hue_rgb(n > 1 ? p[1] : 40.0f, hr, hg, hb);
  const int w = f->width, h = f->height, st = f->stride;
  for (int y = 0; y < h; ++y) {
    uint8_t* row = f->data + y * st;
    for (int x = 0; x < w; ++x) {
      uint8_t* px = row + x * 4;
      const float L = luma(px);
      const float k = 0.3f * std::fabs(L - 128.0f) / 128.0f;
      const float tb = L < 128.0f ? sb : hb;
      const float tg = L < 128.0f ? sg : hg;
      const float tr = L < 128.0f ? sr : hr;
      px[0] = to8(px[0] + (tb - px[0]) * k);
      px[1] = to8(px[1] + (tg - px[1]) * k);
      px[2] = to8(px[2] + (tr - px[2]) * k);
    }
  }
  return 0;
}

}  // namespace fx
