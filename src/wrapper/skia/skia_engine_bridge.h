#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace XER::engine::bridge::skia {

bool IsAvailable();
bool HasNativeIncludes();
bool HasNativeRaster();
std::string Summary();
std::string Version();

using Matrix3x3 = std::array<double, 9>;

std::vector<std::string> ExportedFunctionNames();
bool HasFunction(const std::string& name);
std::vector<std::string> BlendModeNames();

double DegToRad(double degrees);
double RadToDeg(double radians);
double Clamp01(double value);
double Lerp(double from, double to, double t);
double MapRange(double value,
				double in_min,
				double in_max,
				double out_min,
				double out_max);

double Distance(double x1, double y1, double x2, double y2);
std::string PointToString(double x, double y);

uint32_t MakeColorRGBA(int r, int g, int b, int a);
std::string ColorToHex(uint32_t rgba, bool include_alpha);
bool ParseColorHex(const std::string& value, uint32_t* rgba_out);
uint32_t PremultiplyAlpha(uint32_t rgba);
uint32_t UnpremultiplyAlpha(uint32_t rgba);

std::string NormalizeBlendModeName(const std::string& mode_name);
uint32_t BlendSrcOver(uint32_t dst_rgba, uint32_t src_rgba);
uint32_t BlendModeApply(const std::string& mode_name, uint32_t dst_rgba, uint32_t src_rgba);

Matrix3x3 MatrixIdentity();
Matrix3x3 MatrixTranslate(double tx, double ty);
Matrix3x3 MatrixScale(double sx, double sy);
Matrix3x3 MatrixRotateDegrees(double degrees);
Matrix3x3 MatrixMultiply(const Matrix3x3& a, const Matrix3x3& b);
bool MatrixInvert(const Matrix3x3& m, Matrix3x3* out_inv);

bool RasterClear(std::vector<uint32_t>* pixels,
				 int width,
				 int height,
				 uint32_t color);

bool RasterDrawRect(std::vector<uint32_t>* pixels,
					int width,
					int height,
					int x,
					int y,
					int rect_width,
					int rect_height,
					uint32_t color,
					const std::string& blend_mode);

bool RasterDrawLine(std::vector<uint32_t>* pixels,
					int width,
					int height,
					int x0,
					int y0,
					int x1,
					int y1,
					uint32_t color,
					const std::string& blend_mode);

bool RasterDrawCircle(std::vector<uint32_t>* pixels,
					  int width,
					  int height,
					  int cx,
					  int cy,
					  int radius,
					  uint32_t color,
					  const std::string& blend_mode,
					  bool filled);

bool RasterDrawTriangle(std::vector<uint32_t>* pixels,
						int width,
						int height,
						int x1,
						int y1,
						int x2,
						int y2,
						int x3,
						int y3,
						uint32_t color,
						const std::string& blend_mode,
						bool filled);

bool RasterDrawPolyline(std::vector<uint32_t>* pixels,
						int width,
						int height,
						const std::vector<std::pair<int, int>>& points,
						uint32_t color,
						const std::string& blend_mode,
						bool closed);

bool RasterFillPolygon(std::vector<uint32_t>* pixels,
					   int width,
					   int height,
					   const std::vector<std::pair<int, int>>& points,
					   uint32_t color,
					   const std::string& blend_mode);

bool RasterDrawImage(std::vector<uint32_t>* dst_pixels,
					 int dst_width,
					 int dst_height,
					 const std::vector<uint32_t>& src_pixels,
					 int src_width,
					 int src_height,
					 int dx,
					 int dy,
					 const std::string& blend_mode);

// ---------------------------------------------------------------------------
// Path API
// ---------------------------------------------------------------------------

enum class PathVerb : uint8_t {
    kMove,     // x, y
    kLine,     // x, y
    kQuad,     // cx cy x y
    kConic,    // cx cy x y weight
    kCubic,    // c1x c1y c2x c2y x y
    kArcTo,    // rx ry xRotDeg largeArc sweep x y
    kClose,    // (no pts)
};

struct PathCommand {
    PathVerb verb;
    std::vector<float> pts;  // args matching verb
};

using PathData = std::vector<PathCommand>;

bool RasterDrawPath(std::vector<uint32_t>* pixels,
                    int width,
                    int height,
                    const PathData& path,
                    uint32_t color,
                    const std::string& blend_mode,
                    bool filled);

// ---------------------------------------------------------------------------
// Clip
// ---------------------------------------------------------------------------

bool RasterClipRect(std::vector<uint32_t>* pixels,
                    int width,
                    int height,
                    int cx, int cy, int cw, int ch,
                    std::vector<uint32_t>* out_saved);

bool RasterClipRestore(std::vector<uint32_t>* pixels,
                       int width,
                       int height,
                       const std::vector<uint32_t>& saved);

// ---------------------------------------------------------------------------
// Codec: encode / decode
// ---------------------------------------------------------------------------

bool EncodeImagePNG(const std::vector<uint32_t>& pixels,
                    int width,
                    int height,
                    std::vector<uint8_t>* out_bytes);

bool EncodeImageJPEG(const std::vector<uint32_t>& pixels,
                     int width,
                     int height,
                     int quality,
                     std::vector<uint8_t>* out_bytes);

bool DecodeImage(const std::vector<uint8_t>& bytes,
                 int* out_width,
                 int* out_height,
                 std::vector<uint32_t>* out_pixels);

// ===========================================================================
// Image / Pixel processing (Photoshop-level)
// ===========================================================================

// ── Transform ──────────────────────────────────────────────────────────────

bool ImageScale(const std::vector<uint32_t>& src, int src_w, int src_h,
                int dst_w, int dst_h,
                std::vector<uint32_t>* out,
                bool bilinear = true);

bool ImageRotate(const std::vector<uint32_t>& src, int src_w, int src_h,
                 double degrees, uint32_t bg_color,
                 std::vector<uint32_t>* out, int* out_w, int* out_h);

bool ImageFlipH(const std::vector<uint32_t>& src, int src_w, int src_h,
                std::vector<uint32_t>* out);

bool ImageFlipV(const std::vector<uint32_t>& src, int src_w, int src_h,
                std::vector<uint32_t>* out);

bool ImageCrop(const std::vector<uint32_t>& src, int src_w, int src_h,
               int x, int y, int cw, int ch,
               std::vector<uint32_t>* out);

bool ImageComposite(std::vector<uint32_t>* dst, int dst_w, int dst_h,
                    const std::vector<uint32_t>& src, int src_w, int src_h,
                    int dx, int dy,
                    const std::string& blend_mode, float alpha = 1.0f);

// ── Colour adjustments ─────────────────────────────────────────────────────

// Brightness [-255…+255], contrast [-255…+255]
bool AdjustBrightnessContrast(std::vector<uint32_t>* pixels,
                               int width, int height,
                               int brightness, int contrast);

// Hue [-180…+180], saturation [-100…+100], lightness [-100…+100]
bool AdjustHSL(std::vector<uint32_t>* pixels, int width, int height,
               double hue, double saturation, double lightness);

// Exposure in stops, gamma > 0
bool AdjustExposure(std::vector<uint32_t>* pixels, int width, int height,
                    double exposure, double gamma);

// levels: input black/white/gamma, output black/white (all 0-255 except gamma>0)
bool AdjustLevels(std::vector<uint32_t>* pixels, int width, int height,
                  int in_black, int in_white, double gamma,
                  int out_black, int out_white);

// curves: 256-entry lookup table for R/G/B/A channels (nullptr = identity)
bool AdjustCurves(std::vector<uint32_t>* pixels, int width, int height,
                  const uint8_t* lut_r, const uint8_t* lut_g,
                  const uint8_t* lut_b, const uint8_t* lut_a);

// Color balance: shadows/midtones/highlights delta for R,G,B each
bool AdjustColorBalance(std::vector<uint32_t>* pixels, int width, int height,
                        int shadow_r,   int shadow_g,   int shadow_b,
                        int midtone_r,  int midtone_g,  int midtone_b,
                        int hilight_r,  int hilight_g,  int hilight_b);

// Vibrance [-100…+100], saturation boost with skin-tone protection
bool AdjustVibrance(std::vector<uint32_t>* pixels, int width, int height,
                    double vibrance);

// Channel mixer: each output channel = linear combination of R/G/B/const
struct ChannelMixerCoeff { double r, g, b, constant; };
bool AdjustChannelMixer(std::vector<uint32_t>* pixels, int width, int height,
                        const ChannelMixerCoeff& out_r,
                        const ChannelMixerCoeff& out_g,
                        const ChannelMixerCoeff& out_b);

// Replace one colour with another within tolerance
bool ColorReplace(std::vector<uint32_t>* pixels, int width, int height,
                  uint32_t from_color, uint32_t to_color, int tolerance);

// Invert colours
bool AdjustInvert(std::vector<uint32_t>* pixels, int width, int height);

// Convert to greyscale preserving luminosity
bool AdjustGrayscale(std::vector<uint32_t>* pixels, int width, int height);

// Sepia tone
bool AdjustSepia(std::vector<uint32_t>* pixels, int width, int height,
                 double intensity);

// Threshold: pixels above value → white, below → black
bool AdjustThreshold(std::vector<uint32_t>* pixels, int width, int height,
                     int value);

// Posterise: reduce colour levels per channel
bool AdjustPosterize(std::vector<uint32_t>* pixels, int width, int height,
                     int levels);

// Global opacity
bool AdjustOpacity(std::vector<uint32_t>* pixels, int width, int height,
                   float opacity);

// Selective color: for named range ("reds","yellows",…) adjust CMYK deltas
bool AdjustSelectiveColor(std::vector<uint32_t>* pixels, int width, int height,
                          const std::string& range,
                          int dc, int dm, int dy, int dk);

// ── Filters ────────────────────────────────────────────────────────────────

// Box blur, radius ≥ 1
bool FilterBlur(std::vector<uint32_t>* pixels, int width, int height,
                int radius_x, int radius_y);

// Gaussian blur
bool FilterGaussianBlur(std::vector<uint32_t>* pixels, int width, int height,
                        double sigma_x, double sigma_y);

// Motion blur: angle (degrees), distance
bool FilterMotionBlur(std::vector<uint32_t>* pixels, int width, int height,
                      double angle, int distance);

// Radial blur: spin amount (0-360 steps), zoom (0-100%)
bool FilterRadialBlur(std::vector<uint32_t>* pixels, int width, int height,
                      double amount, bool zoom_mode);

// Sharpen (unsharp mask)
bool FilterSharpen(std::vector<uint32_t>* pixels, int width, int height,
                   double amount, double sigma, int threshold);

// Emboss
bool FilterEmboss(std::vector<uint32_t>* pixels, int width, int height,
                  double angle, double strength);

// Edge detect (Sobel)
bool FilterEdgeDetect(std::vector<uint32_t>* pixels, int width, int height);

// Noise add: amount 0-255, gaussian vs uniform, monochrome flag
bool FilterAddNoise(std::vector<uint32_t>* pixels, int width, int height,
                    int amount, bool gaussian, bool monochrome);

// Median filter for de-noising
bool FilterMedian(std::vector<uint32_t>* pixels, int width, int height,
                  int radius);

// Pixelate
bool FilterPixelate(std::vector<uint32_t>* pixels, int width, int height,
                    int cell_size);

// Oil paint effect (bilateral-like)
bool FilterOilPaint(std::vector<uint32_t>* pixels, int width, int height,
                    int brush_size, int intensity);

// Vignette: strength 0-1, feather 0-1
bool FilterVignette(std::vector<uint32_t>* pixels, int width, int height,
                    double strength, double feather);

// Chromatic aberration
bool FilterChromaticAberration(std::vector<uint32_t>* pixels, int width, int height,
                                int red_offset_x, int red_offset_y,
                                int blue_offset_x, int blue_offset_y);

// Convolution with arbitrary kernel
bool FilterConvolve(std::vector<uint32_t>* pixels, int width, int height,
                    const std::vector<double>& kernel, int kw, int kh,
                    double divisor, double bias);

// ── Drawing enhancements ───────────────────────────────────────────────────

struct GradientStop { float pos; uint32_t color; };
using GradientStops = std::vector<GradientStop>;

bool RasterLinearGradient(std::vector<uint32_t>* pixels, int width, int height,
                          float x0, float y0, float x1, float y1,
                          const GradientStops& stops,
                          const std::string& blend_mode);

bool RasterRadialGradient(std::vector<uint32_t>* pixels, int width, int height,
                          float cx, float cy, float radius,
                          const GradientStops& stops,
                          const std::string& blend_mode);

bool RasterSweepGradient(std::vector<uint32_t>* pixels, int width, int height,
                         float cx, float cy, float start_angle,
                         const GradientStops& stops,
                         const std::string& blend_mode);

// Flood fill (magic-wand style)
bool RasterFloodFill(std::vector<uint32_t>* pixels, int width, int height,
                     int seed_x, int seed_y,
                     uint32_t fill_color, int tolerance);

// Draw rounded rectangle
bool RasterDrawRoundRect(std::vector<uint32_t>* pixels, int width, int height,
                         int x, int y, int rw, int rh,
                         int rx, int ry,
                         uint32_t color, const std::string& blend_mode, bool filled);

// Draw ellipse
bool RasterDrawEllipse(std::vector<uint32_t>* pixels, int width, int height,
                       int cx, int cy, int rx, int ry,
                       uint32_t color, const std::string& blend_mode, bool filled);

// Draw arc (open curve)
bool RasterDrawArc(std::vector<uint32_t>* pixels, int width, int height,
                   int cx, int cy, int rx, int ry,
                   double start_deg, double sweep_deg,
                   uint32_t color, const std::string& blend_mode);

// Draw with stroke width > 1
bool RasterDrawThickLine(std::vector<uint32_t>* pixels, int width, int height,
                         float x0, float y0, float x1, float y1,
                         float stroke_width,
                         uint32_t color, const std::string& blend_mode);

// Shadow: offset + blur for a shape
bool RasterDropShadow(std::vector<uint32_t>* dst, int width, int height,
                      const std::vector<uint32_t>& src, int src_w, int src_h,
                      int offset_x, int offset_y,
                      double blur_sigma, uint32_t shadow_color);

// Inner glow / outer glow
bool RasterGlow(std::vector<uint32_t>* pixels, int width, int height,
                double radius, uint32_t color, bool inner, float strength);

// ── Text / Font ─────────────────────────────────────────────────────────────

struct FontOptions {
    std::string family;
    double size;
    int weight;       // 100-900
    bool italic;
    bool underline;
    bool strikethrough;
};

bool RasterDrawText(std::vector<uint32_t>* pixels, int width, int height,
                    const std::string& text, float x, float y,
                    const FontOptions& font,
                    uint32_t color, const std::string& blend_mode);

bool MeasureText(const std::string& text, const FontOptions& font,
                 float* out_width, float* out_height, float* out_baseline);

// ── Mask / Selection ────────────────────────────────────────────────────────

// Apply a greyscale mask (255 = fully visible, 0 = transparent)
bool ApplyMask(std::vector<uint32_t>* pixels, int width, int height,
               const std::vector<uint8_t>& mask_grey);

// Erode / dilate alpha mask
bool MaskErode(std::vector<uint8_t>* mask, int width, int height, int radius);
bool MaskDilate(std::vector<uint8_t>* mask, int width, int height, int radius);

// Build mask from colour range (magic select)
bool MaskFromColorRange(const std::vector<uint32_t>& pixels, int width, int height,
                        uint32_t seed_color, int tolerance,
                        std::vector<uint8_t>* out_mask);

// ── Histogram ───────────────────────────────────────────────────────────────

struct Histogram {
    std::vector<uint32_t> r, g, b, a, luma;  // 256 entries each
};

bool ComputeHistogram(const std::vector<uint32_t>& pixels, int width, int height,
                      Histogram* out);

bool AutoLevels(std::vector<uint32_t>* pixels, int width, int height,
                double clip_percent = 0.1);

bool EqualizeHistogram(std::vector<uint32_t>* pixels, int width, int height);

}  // namespace XER::engine::bridge::skia