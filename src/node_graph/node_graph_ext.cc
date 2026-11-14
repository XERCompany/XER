// Extended node library: complete math coverage, full string processing
// (re2 regex, icu unicode case mapping, zlib crc/compress, range-v3 views)
// and block-based audio DSP nodes operating on Value::Array sample blocks.

#include "node_graph/node_graph_registry.h"

#include <re2/re2.h>
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <zlib.h>

#ifdef XER_HAVE_MKL
#include <mkl_cblas.h>
#include <mkl_dfti.h>
#include <mkl_types.h>
#include <mkl_vml.h>
#endif

#include <range/v3/algorithm/sort.hpp>
#include <range/v3/view/reverse.hpp>
#include <range/v3/view/transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace XER::NodeGraph {
namespace {

constexpr double kPi = 3.14159265358979323846;

PortSpec In(const std::string& name, ValueType type, Value def = Value()) {
    return {name, type, std::move(def)};
}
PortSpec Out(const std::string& name, ValueType type) { return {name, type, Value()}; }

NodeTypeDescriptor Make(std::string type, std::string title, std::string category,
                        std::string description, std::vector<PortSpec> inputs,
                        std::vector<PortSpec> outputs, EvaluateFn fn,
                        std::vector<ParamSpec> params = {}, bool pure = true) {
    NodeTypeDescriptor d;
    d.type = std::move(type);
    d.title = std::move(title);
    d.category = std::move(category);
    d.description = std::move(description);
    d.inputs = std::move(inputs);
    d.outputs = std::move(outputs);
    d.evaluate = std::move(fn);
    d.params = std::move(params);
    d.pure = pure;
    return d;
}

template <typename Op>
void Unary(NodeTypeRegistry& r, const std::string& type, const std::string& title,
           const std::string& desc, Op op) {
    r.Register(Make(type, title, "Math", desc, {In("x", ValueType::kNumber, Value(0.0))},
                    {Out("result", ValueType::kNumber)}, [op](EvalContext& c) {
                        c.Output("result", op(c.Input("x").AsNumber()));
                        return true;
                    }));
}

template <typename Op>
void Binary(NodeTypeRegistry& r, const std::string& type, const std::string& title,
            const std::string& desc, Op op, double default_b = 0.0) {
    r.Register(Make(type, title, "Math", desc,
                    {In("a", ValueType::kNumber, Value(0.0)),
                     In("b", ValueType::kNumber, Value(default_b))},
                    {Out("result", ValueType::kNumber)}, [op](EvalContext& c) {
                        c.Output("result",
                                 op(c.Input("a").AsNumber(), c.Input("b").AsNumber()));
                        return true;
                    }));
}

// ── UTF-8 helpers ──────────────────────────────────────────────────────────

std::vector<std::uint32_t> Utf8Decode(const std::string& s) {
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = c;
        std::size_t len = 1;
        if ((c & 0x80) == 0) {
        } else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { ++i; continue; }  // invalid byte: skip
        if (i + len > s.size()) { ++i; continue; }
        for (std::size_t j = 1; j < len; ++j) {
            const unsigned char cc = static_cast<unsigned char>(s[i + j]);
            if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string Utf8Encode(const std::vector<std::uint32_t>& cps) {
    std::string out;
    for (std::uint32_t cp : cps) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// ICU-backed full Unicode case mapping (handles non-ASCII correctly).
std::string UnicodeCaseMap(const std::string& utf8, bool to_upper) {
    UErrorCode status = U_ZERO_ERROR;
    int32_t u16_len = 0;
    u_strFromUTF8(nullptr, 0, &u16_len, utf8.data(),
                  static_cast<int32_t>(utf8.size()), &status);
    if (U_FAILURE(status) || u16_len <= 0) return utf8;
    status = U_ZERO_ERROR;
    std::vector<UChar> buf(static_cast<std::size_t>(u16_len) + 1);
    u_strFromUTF8(buf.data(), u16_len + 1, nullptr, utf8.data(),
                  static_cast<int32_t>(utf8.size()), &status);
    if (U_FAILURE(status)) return utf8;
    status = U_ZERO_ERROR;
    std::vector<UChar> mapped(static_cast<std::size_t>(u16_len) + 64);
    const int32_t mapped_len = to_upper
        ? u_strToUpper(mapped.data(), static_cast<int32_t>(mapped.size()), buf.data(), u16_len, nullptr, &status)
        : u_strToLower(mapped.data(), static_cast<int32_t>(mapped.size()), buf.data(), u16_len, nullptr, &status);
    if (U_FAILURE(status) || mapped_len <= 0) return utf8;
    status = U_ZERO_ERROR;
    int32_t out_len = 0;
    u_strToUTF8(nullptr, 0, &out_len, mapped.data(), mapped_len, &status);
    if (U_FAILURE(status) || out_len <= 0) return utf8;
    status = U_ZERO_ERROR;
    std::string out(static_cast<std::size_t>(out_len), '\0');
    u_strToUTF8(out.data(), out_len + 1, nullptr, mapped.data(), mapped_len, &status);
    if (U_FAILURE(status)) return utf8;
    out.resize(static_cast<std::size_t>(out_len));
    return out;
}

// ── Audio DSP helpers (operate on mono float blocks) ───────────────────────

double DbToLin(double db) { return std::pow(10.0, db / 20.0); }
double LinToDb(double x) { return x > 1e-12 ? 20.0 * std::log10(x) : -120.0; }

// Portable BLAS replacements — used when MKL is unavailable (macOS, minimal
// Linux installs). MKL builds keep the vectorized calls.
inline void ScaleInPlace(double* x, std::size_t n, double alpha) {
#ifdef XER_HAVE_MKL
    if (n > 0) cblas_dscal(static_cast<int>(n), alpha, x, 1);
#else
    for (std::size_t i = 0; i < n; ++i) x[i] *= alpha;
#endif
}

inline double NormL2(const double* x, std::size_t n) {
#ifdef XER_HAVE_MKL
    return cblas_dnrm2(static_cast<int>(n), x, 1);
#else
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += x[i] * x[i];
    return std::sqrt(s);
#endif
}

// 1-based index of max |x[i]| — same convention as cblas_idamax.
inline int IndexOfMaxAbs(const double* x, std::size_t n) {
#ifdef XER_HAVE_MKL
    return static_cast<int>(cblas_idamax(static_cast<int>(n), x, 1));
#else
    int best = 0;
    double best_abs = -1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double a = std::fabs(x[i]);
        if (a > best_abs) {
            best_abs = a;
            best = static_cast<int>(i) + 1;
        }
    }
    return best;
#endif
}

// ── MKL-accelerated array ops ──────────────────────────────────────────────

// FFT convolution via MKL DFTI (double, real input/output).
// out[i] = sum_j s[i+j] * k[j]  ==  conv(s, reversed k)[i + k.size() - 1].
// Returns false when the transform cannot run — caller falls back to the
// direct O(N*K) loop.
bool ConvolveFft(const std::vector<double>& s, const std::vector<double>& k,
                 std::vector<double>& out) {
#ifdef XER_HAVE_MKL
    const std::size_t n = s.size();
    const std::size_t m = k.size();
    if (n == 0 || m == 0) return false;
    const std::size_t need = n + m - 1;
    std::size_t nfft = 1;
    while (nfft < need) nfft <<= 1;
    if (nfft > (std::size_t{1} << 25)) return false;  // >33M points: too much RAM

    std::vector<double> s_pad(nfft, 0.0), k_pad(nfft, 0.0), real_out(nfft);
    std::copy(s.begin(), s.end(), s_pad.begin());
    for (std::size_t j = 0; j < m; ++j) k_pad[j] = k[m - 1 - j];

    const std::size_t nh = nfft / 2 + 1;
    std::vector<MKL_Complex16> S(nh), K(nh), C(nh);

    DFTI_DESCRIPTOR_HANDLE desc = nullptr;
    if (DftiCreateDescriptor(&desc, DFTI_DOUBLE, DFTI_REAL, 1,
                             static_cast<MKL_LONG>(nfft)) != DFTI_NO_ERROR ||
        DftiCommitDescriptor(desc) != DFTI_NO_ERROR) {
        if (desc != nullptr) DftiFreeDescriptor(&desc);
        return false;
    }
    bool ok = DftiComputeForward(desc, s_pad.data(), S.data()) == DFTI_NO_ERROR &&
              DftiComputeForward(desc, k_pad.data(), K.data()) == DFTI_NO_ERROR;
    if (ok) {
        for (std::size_t i = 0; i < nh; ++i) {
            C[i].real = S[i].real * K[i].real - S[i].imag * K[i].imag;
            C[i].imag = S[i].real * K[i].imag + S[i].imag * K[i].real;
        }
        // MKL backward is unnormalized — scaled by 1/nfft below.
        ok = DftiComputeBackward(desc, C.data(), real_out.data()) == DFTI_NO_ERROR;
    }
    DftiFreeDescriptor(&desc);
    if (!ok) return false;

    out.assign(n, 0.0);
    const double inv = 1.0 / static_cast<double>(nfft);
    for (std::size_t i = 0; i < n; ++i) out[i] = real_out[i + m - 1] * inv;
    return true;
#else
    (void)s;
    (void)k;
    (void)out;
    return false;  // no MKL — caller falls back to the direct loop
#endif
}

// sin(arg) for arg[i] = phase0 + step*i via MKL VML (vdSin = radians,
// double) — used by LFO-driven nodes instead of per-sample libm sin.
void FillSinRamp(double* out, std::size_t n, double step, double phase0) {
    if (n == 0) return;
#ifdef XER_HAVE_MKL
    std::vector<double> args(n);
    for (std::size_t i = 0; i < n; ++i) args[i] = phase0 + step * i;
    vdSin(static_cast<MKL_INT>(n), args.data(), out);
#else
    for (std::size_t i = 0; i < n; ++i)
        out[i] = std::sin(phase0 + step * static_cast<double>(i));
#endif
}

// RBJ Audio EQ Cookbook biquad, processed over a whole block.
void BiquadProcess(Value::Array& x, const std::string& type, double freq,
                   double q, double gain_db, double sample_rate) {
    freq = std::clamp(freq, 10.0, sample_rate * 0.45);
    q = std::clamp(q, 0.1, 18.0);
    const double A = std::pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * kPi * freq / sample_rate;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);

    double b0, b1, b2, a0, a1, a2;
    if (type == "highpass") {
        b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else if (type == "bandpass") {
        b0 = alpha; b1 = 0; b2 = -alpha;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else if (type == "notch") {
        b0 = 1; b1 = -2 * cw; b2 = 1;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else if (type == "peaking") {
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
    } else if (type == "lowshelf") {
        const double s = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cw + s);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - s;
    } else if (type == "highshelf") {
        const double s = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cw + s);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - s;
    } else {  // lowpass
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (double& s : x) {
        const double in = s;
        const double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = in; y2 = y1; y1 = out;
        s = out;
    }
}

// Fractional-delay line with linear interpolation (shared by delay-based FX).
struct DelayLine {
    std::vector<double> buf;
    std::size_t pos = 0;
    void Resize(std::size_t n) {
        if (buf.size() < n) buf.assign(n, 0.0);
    }
    double Read(double delay_samples) const {
        const double d = std::max(0.0, delay_samples);
        const double i0 = std::floor(d);
        const double frac = d - i0;
        const std::size_t n = buf.size();
        if (n == 0) return 0.0;
        auto at = [&](double back) {
            long idx = static_cast<long>(pos) - 1 - static_cast<long>(back);
            idx %= static_cast<long>(n);
            if (idx < 0) idx += static_cast<long>(n);
            return buf[static_cast<std::size_t>(idx)];
        };
        const double a = at(i0), b = at(i0 + 1.0);
        return a + (b - a) * frac;
    }
    void Write(double v) {
        if (buf.empty()) return;
        buf[pos] = v;
        pos = (pos + 1) % buf.size();
    }
};

// Modulated delay for chorus / flanger / vibrato.
void ModDelayProcess(Value::Array& x, double sample_rate, double rate_hz,
                     double depth_ms, double feedback, double mix,
                     bool invert_feedback) {
    const std::size_t n = x.size();
    if (n == 0) return;
    const double max_delay = 0.12 * sample_rate;  // 120 ms headroom
    DelayLine dl;
    dl.Resize(static_cast<std::size_t>(max_delay) + 8);
    const double base_delay = 0.012 * sample_rate;  // 12 ms centre
    const double depth = depth_ms * 0.001 * sample_rate;
    const double fb = std::clamp(feedback, -0.95, 0.95) * (invert_feedback ? -1.0 : 1.0);
    // LFO values are delay-independent — compute the whole ramp with MKL VML.
    std::vector<double> lfo(n);
    FillSinRamp(lfo.data(), n, 2.0 * kPi * rate_hz / sample_rate, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double delayed = dl.Read(base_delay + depth * lfo[i]);
        const double fb_sample = delayed * fb;
        const double wet = delayed;
        dl.Write(x[i] + fb_sample);
        x[i] = x[i] * (1.0 - mix) + wet * mix;
    }
}

// Freeverb-style mono reverb (4 comb + 2 allpass, damping in feedback path).
void ReverbProcess(Value::Array& x, double sample_rate, double room_size,
                   double damping, double wet_mix) {
    const std::size_t n = x.size();
    if (n == 0) return;
    const double scale = sample_rate / 44100.0;
    const int comb_tunings[] = {1116, 1188, 1277, 1356, 1422, 1491};
    const int ap_tunings[] = {556, 441, 341};
    const double room = 0.7 + 0.28 * std::clamp(room_size, 0.0, 1.0);
    const double damp = std::clamp(damping, 0.0, 1.0);

    struct Comb { std::vector<double> buf; std::size_t pos = 0; double filt = 0; };
    struct Allpass { std::vector<double> buf; std::size_t pos = 0; };
    std::vector<Comb> combs;
    for (int t : comb_tunings) combs.push_back({std::vector<double>(static_cast<std::size_t>(t * scale) + 2, 0.0), 0, 0});
    std::vector<Allpass> aps;
    for (int t : ap_tunings) aps.push_back({std::vector<double>(static_cast<std::size_t>(t * scale) + 2, 0.0), 0});

    for (std::size_t i = 0; i < n; ++i) {
        const double in = x[i];
        double acc = 0.0;
        for (auto& c : combs) {
            const double out = c.buf[c.pos];
            c.filt = out * (1.0 - damp) + c.filt * damp;
            c.buf[c.pos] = in + c.filt * room;
            c.pos = (c.pos + 1) % c.buf.size();
            acc += out;
        }
        for (auto& a : aps) {
            const double bufout = a.buf[a.pos];
            const double out = -acc + bufout;
            a.buf[a.pos] = acc + bufout * 0.5;
            a.pos = (a.pos + 1) % a.buf.size();
            acc = out;
        }
        x[i] = in * (1.0 - wet_mix) + acc * wet_mix / static_cast<double>(combs.size());
    }
}

std::vector<double> AsSignal(const Value& v) { return v.AsArray(); }

std::string FormatNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

}  // namespace

void RegisterExtendedNodes(NodeTypeRegistry& r) {
    using VT = ValueType;

    // ══ Math — full function coverage ════════════════════════════════════
    Unary(r, "math.tan", "Tan", "tan(x)", [](double x) { return std::tan(x); });
    Unary(r, "math.asin", "ASin", "asin(x)", [](double x) {
        return std::abs(x) > 1.0 ? 0.0 : std::asin(x);
    });
    Unary(r, "math.acos", "ACos", "acos(x)", [](double x) {
        return std::abs(x) > 1.0 ? 0.0 : std::acos(x);
    });
    Unary(r, "math.atan", "ATan", "atan(x)", [](double x) { return std::atan(x); });
    Binary(r, "math.atan2", "ATan2", "atan2(y, x)", [](double y, double x) {
        return std::atan2(y, x);
    });
    Unary(r, "math.exp", "Exp", "e^x", [](double x) { return std::exp(std::clamp(x, -700.0, 700.0)); });
    Unary(r, "math.ln", "Ln", "natural log", [](double x) { return x > 0 ? std::log(x) : 0.0; });
    Unary(r, "math.log10", "Log10", "log10(x)", [](double x) { return x > 0 ? std::log10(x) : 0.0; });
    Unary(r, "math.log2", "Log2", "log2(x)", [](double x) { return x > 0 ? std::log2(x) : 0.0; });
    Unary(r, "math.ceil", "Ceil", "ceil(x)", [](double x) { return std::ceil(x); });
    Unary(r, "math.round", "Round", "round(x)", [](double x) { return std::round(x); });
    Unary(r, "math.trunc", "Trunc", "trunc(x)", [](double x) { return std::trunc(x); });
    Unary(r, "math.sign", "Sign", "sign(x)", [](double x) {
        return (x > 0) - (x < 0);
    });
    Unary(r, "math.degrees", "To Degrees", "rad -> deg", [](double x) { return x * 180.0 / kPi; });
    Unary(r, "math.radians", "To Radians", "deg -> rad", [](double x) { return x * kPi / 180.0; });
    Unary(r, "math.sinh", "Sinh", "sinh(x)", [](double x) { return std::sinh(std::clamp(x, -350.0, 350.0)); });
    Unary(r, "math.cosh", "Cosh", "cosh(x)", [](double x) { return std::cosh(std::clamp(x, -350.0, 350.0)); });
    Unary(r, "math.tanh", "Tanh", "tanh(x)", [](double x) { return std::tanh(x); });
    Unary(r, "math.cbrt", "Cbrt", "cbrt(x)", [](double x) { return std::cbrt(x); });
    Binary(r, "math.hypot", "Hypot", "sqrt(a^2 + b^2)", [](double a, double b) {
        return std::hypot(a, b);
    });
    Unary(r, "math.frac", "Frac", "x - floor(x)", [](double x) { return x - std::floor(x); });
    Unary(r, "math.wrap01", "Wrap 0..1", "wrap x into [0, 1)", [](double x) {
        const double f = x - std::floor(x);
        return f < 0 ? f + 1.0 : f;
    });
    Unary(r, "math.smoothstep", "Smoothstep", "t*t*(3-2t)", [](double x) {
        const double t = std::clamp(x, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    });
    Unary(r, "math.square", "Square", "x*x", [](double x) { return x * x; });
    Unary(r, "math.reciprocal", "1/x", "1 / x (0 when x == 0)", [](double x) {
        return x == 0.0 ? 0.0 : 1.0 / x;
    });

    // Math constants (zero-input nodes).
    auto const_node = [&](const char* type, const char* title, double v) {
        r.Register(Make(type, title, "Math", "Constant " + std::string(title), {},
                        {Out("value", VT::kNumber)}, [v](EvalContext& c) {
                            c.Output("value", v);
                            return true;
                        }));
    };
    const_node("math.pi", "Pi", kPi);
    const_node("math.tau", "Tau", 2.0 * kPi);
    const_node("math.e", "Euler's Number", 2.71828182845904523536);

    // ══ String — re2 / icu / zlib / range-v3 ══════════════════════════════
    r.Register(Make("string.lower", "Lower Case", "String",
                    "Unicode-aware lower case (icu)", {In("text", VT::kString, Value(""))},
                    {Out("result", VT::kString)}, [](EvalContext& c) {
                        c.Output("result", UnicodeCaseMap(c.Input("text").AsString(), false));
                        return true;
                    }));
    // Override the ASCII-only builtin with full Unicode upper case.
    r.Register(Make("string.upper", "Upper Case", "String",
                    "Unicode-aware upper case (icu)", {In("text", VT::kString, Value(""))},
                    {Out("result", VT::kString)}, [](EvalContext& c) {
                        c.Output("result", UnicodeCaseMap(c.Input("text").AsString(), true));
                        return true;
                    }));
    r.Register(Make("string.trim", "Trim", "String", "Removes leading/trailing whitespace",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        std::string s = c.Input("text").AsString();
                        const auto is_ws = [](unsigned char ch) {
                            return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
                                   ch == '\f' || ch == '\v';
                        };
                        std::size_t b = 0, e = s.size();
                        while (b < e && is_ws(static_cast<unsigned char>(s[b]))) ++b;
                        while (e > b && is_ws(static_cast<unsigned char>(s[e - 1]))) --e;
                        c.Output("result", s.substr(b, e - b));
                        return true;
                    }));
    r.Register(Make("string.reverse", "Reverse", "String", "Reverses UTF-8 text by codepoint",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        auto cps = Utf8Decode(c.Input("text").AsString());
                        std::reverse(cps.begin(), cps.end());
                        c.Output("result", Utf8Encode(cps));
                        return true;
                    }));
    r.Register(Make("string.substring", "Substring", "String",
                    "text[start .. start+length) in codepoints (negative start = from end)",
                    {In("text", VT::kString, Value("")), In("start", VT::kNumber, Value(0.0)),
                     In("length", VT::kNumber, Value(-1.0))},
                    {Out("result", VT::kString)}, [](EvalContext& c) {
                        auto cps = Utf8Decode(c.Input("text").AsString());
                        const long n = static_cast<long>(cps.size());
                        long start = static_cast<long>(c.Input("start").AsNumber());
                        if (start < 0) start += n;
                        start = std::clamp(start, 0L, n);
                        long len = static_cast<long>(c.Input("length").AsNumber());
                        if (len < 0 || start + len > n) len = n - start;
                        len = std::max(0L, len);
                        const std::vector<std::uint32_t> slice(cps.begin() + start,
                                                               cps.begin() + start + len);
                        c.Output("result", Utf8Encode(slice));
                        return true;
                    }));
    r.Register(Make("string.repeat", "Repeat", "String", "Repeats text count times",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        const std::string s = c.Input("text").AsString();
                        const long count = std::clamp(
                            static_cast<long>(c.Param("count").AsNumber()), 0L, 100000L);
                        std::string out;
                        out.reserve(s.size() * static_cast<std::size_t>(count));
                        for (long i = 0; i < count; ++i) out += s;
                        c.Output("result", out);
                        return true;
                    },
                    {{"count", Value(2.0), {}}}));
    r.Register(Make("string.pad", "Pad", "String", "Pads text to width",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        std::string s = c.Input("text").AsString();
                        const auto cps = Utf8Decode(s);
                        const long width = std::max(0L, static_cast<long>(c.Param("width").AsNumber()));
                        const long missing = width - static_cast<long>(cps.size());
                        if (missing <= 0) { c.Output("result", s); return true; }
                        const std::string fill(1, c.Param("fill").AsString().empty()
                                                     ? ' ' : c.Param("fill").AsString()[0]);
                        const std::string pad(static_cast<std::size_t>(missing), fill[0]);
                        const std::string align = c.Param("align").AsString();
                        if (align == "right") s = pad + s;
                        else if (align == "center") {
                            const long left = missing / 2;
                            s = std::string(static_cast<std::size_t>(left), fill[0]) + s +
                                std::string(static_cast<std::size_t>(missing - left), fill[0]);
                        } else s += pad;
                        c.Output("result", s);
                        return true;
                    },
                    {{"width", Value(10.0), {}},
                     {"align", Value("left"), {"left", "right", "center"}},
                     {"fill", Value(" "), {}}}));
    r.Register(Make("string.contains", "Contains", "String", "text contains needle",
                    {In("text", VT::kString, Value("")), In("needle", VT::kString, Value(""))},
                    {Out("result", VT::kBool)}, [](EvalContext& c) {
                        c.Output("result", c.Input("text").AsString().find(c.Input("needle").AsString()) !=
                                              std::string::npos);
                        return true;
                    }));
    r.Register(Make("string.starts_with", "Starts With", "String",
                    "True when text begins with prefix",
                    {In("text", VT::kString, Value("")), In("prefix", VT::kString, Value(""))},
                    {Out("result", VT::kBool)}, [](EvalContext& c) {
                        const std::string t = c.Input("text").AsString();
                        const std::string p = c.Input("prefix").AsString();
                        c.Output("result", t.size() >= p.size() && t.compare(0, p.size(), p) == 0);
                        return true;
                    }));
    r.Register(Make("string.ends_with", "Ends With", "String",
                    "True when text ends with suffix",
                    {In("text", VT::kString, Value("")), In("suffix", VT::kString, Value(""))},
                    {Out("result", VT::kBool)}, [](EvalContext& c) {
                        const std::string t = c.Input("text").AsString();
                        const std::string p = c.Input("suffix").AsString();
                        c.Output("result", t.size() >= p.size() &&
                                              t.compare(t.size() - p.size(), p.size(), p) == 0);
                        return true;
                    }));
    r.Register(Make("string.index_of", "Index Of", "String", "Byte offset of needle (-1)",
                    {In("text", VT::kString, Value("")), In("needle", VT::kString, Value(""))},
                    {Out("index", VT::kNumber)}, [](EvalContext& c) {
                        const auto pos = c.Input("text").AsString().find(c.Input("needle").AsString());
                        c.Output("index", pos == std::string::npos ? -1.0 : static_cast<double>(pos));
                        return true;
                    }));
    r.Register(Make("string.replace", "Replace", "String", "Literal find/replace (all occurrences)",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        std::string s = c.Input("text").AsString();
                        const std::string from = c.Param("find").AsString();
                        const std::string to = c.Param("replace").AsString();
                        if (!from.empty()) {
                            for (std::size_t pos = s.find(from); pos != std::string::npos;
                                 pos = s.find(from, pos + to.size())) {
                                s.replace(pos, from.size(), to);
                            }
                        }
                        c.Output("result", s);
                        return true;
                    },
                    {{"find", Value(""), {}}, {"replace", Value(""), {}}}));
    r.Register(Make("string.regex_replace", "Regex Replace", "String",
                    "re2 pattern -> replacement (\\1 backrefs)",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        std::string s = c.Input("text").AsString();
                        RE2 re(c.Param("pattern").AsString());
                        if (re.ok()) {
                            RE2::GlobalReplace(&s, re, c.Param("replacement").AsString());
                        }
                        c.Output("result", s);
                        return true;
                    },
                    {{"pattern", Value(""), {}}, {"replacement", Value(""), {}}}));
    r.Register(Make("string.regex_match", "Regex Match", "String",
                    "re2 partial match -> bool",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kBool)},
                    [](EvalContext& c) {
                        RE2 re(c.Param("pattern").AsString());
                        c.Output("result",
                                 re.ok() && RE2::PartialMatch(c.Input("text").AsString(), re));
                        return true;
                    },
                    {{"pattern", Value(""), {}}}));
    r.Register(Make("string.split_count", "Split Count", "String",
                    "Number of non-overlapping occurrences of separator",
                    {In("text", VT::kString, Value("")), In("sep", VT::kString, Value(","))},
                    {Out("count", VT::kNumber)}, [](EvalContext& c) {
                        const std::string s = c.Input("text").AsString();
                        const std::string sep = c.Input("sep").AsString();
                        if (sep.empty()) { c.Output("count", 0.0); return true; }
                        double count = 0;
                        for (std::size_t pos = s.find(sep); pos != std::string::npos;
                             pos = s.find(sep, pos + sep.size())) ++count;
                        c.Output("count", count);
                        return true;
                    }));
    r.Register(Make("string.join", "Join Numbers", "String",
                    "Joins an array of numbers with a separator",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        const auto arr = c.Input("array").AsArray();
                        const std::string sep = c.Param("separator").AsString();
                        std::string out;
                        for (std::size_t i = 0; i < arr.size(); ++i) {
                            if (i) out += sep;
                            out += FormatNumber(arr[i]);
                        }
                        c.Output("result", out);
                        return true;
                    },
                    {{"separator", Value(", "), {}}}));
    r.Register(Make("string.to_number", "To Number", "String", "Parses a number (strtod)",
                    {In("text", VT::kString, Value(""))}, {Out("value", VT::kNumber)},
                    [](EvalContext& c) {
                        const std::string s = c.Input("text").AsString();
                        char* end = nullptr;
                        const double v = std::strtod(s.c_str(), &end);
                        c.Output("value", end != s.c_str() ? v : 0.0);
                        return true;
                    }));
    r.Register(Make("string.from_number", "From Number", "String", "Formats a number",
                    {In("value", VT::kNumber, Value(0.0))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        c.Output("result", FormatNumber(c.Input("value").AsNumber()));
                        return true;
                    }));
    r.Register(Make("string.length_utf8", "UTF-8 Length", "String",
                    "Number of Unicode codepoints", {In("text", VT::kString, Value(""))},
                    {Out("length", VT::kNumber)}, [](EvalContext& c) {
                        c.Output("length", static_cast<double>(
                                     Utf8Decode(c.Input("text").AsString()).size()));
                        return true;
                    }));
    r.Register(Make("string.crc32", "CRC32", "String", "zlib crc32 checksum",
                    {In("text", VT::kString, Value(""))}, {Out("crc", VT::kNumber)},
                    [](EvalContext& c) {
                        const std::string s = c.Input("text").AsString();
                        c.Output("crc", static_cast<double>(
                                     crc32(0L, reinterpret_cast<const Bytef*>(s.data()),
                                           static_cast<uInt>(s.size()))));
                        return true;
                    }));
    r.Register(Make("string.compress", "Compress", "String",
                    "zlib deflate -> byte array", {In("text", VT::kString, Value(""))},
                    {Out("bytes", VT::kArray)}, [](EvalContext& c) {
                        const std::string s = c.Input("text").AsString();
                        uLongf bound = compressBound(static_cast<uLong>(s.size()));
                        std::vector<Bytef> buf(bound);
                        if (s.empty() ||
                            compress2(buf.data(), &bound,
                                      reinterpret_cast<const Bytef*>(s.data()),
                                      static_cast<uLong>(s.size()), Z_BEST_SPEED) != Z_OK) {
                            c.Output("bytes", Value::Array{});
                            return true;
                        }
                        Value::Array out(bound);
                        for (uLongf i = 0; i < bound; ++i) out[i] = buf[i];
                        c.Output("bytes", std::move(out));
                        return true;
                    }));
    r.Register(Make("string.decompress", "Decompress", "String",
                    "zlib inflate: byte array -> text",
                    {In("bytes", VT::kArray, Value(Value::Array{}))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        const auto arr = c.Input("bytes").AsArray();
                        std::vector<Bytef> in(arr.size());
                        for (std::size_t i = 0; i < arr.size(); ++i)
                            in[i] = static_cast<Bytef>(std::clamp(arr[i], 0.0, 255.0));
                        std::vector<char> out(arr.size() * 8 + 1024);
                        uLongf out_len = static_cast<uLongf>(out.size());
                        const int rc = uncompress(reinterpret_cast<Bytef*>(out.data()), &out_len,
                                                  in.data(), static_cast<uLong>(in.size()));
                        if (rc != Z_OK) { c.Output("result", ""); return true; }
                        c.Output("result", std::string(out.data(), out_len));
                        return true;
                    }));
    r.Register(Make("string.base64_encode", "Base64 Encode", "String",
                    "RFC 4648 base64 encoding",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        static const char* tbl =
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                        const std::string s = c.Input("text").AsString();
                        std::string out;
                        out.reserve((s.size() + 2) / 3 * 4);
                        for (std::size_t i = 0; i < s.size(); i += 3) {
                            const unsigned a = static_cast<unsigned char>(s[i]);
                            const unsigned b = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
                            const unsigned c_byte = i + 2 < s.size() ? static_cast<unsigned char>(s[i + 2]) : 0;
                            const unsigned v = (a << 16) | (b << 8) | c_byte;
                            out += tbl[(v >> 18) & 63];
                            out += tbl[(v >> 12) & 63];
                            out += i + 1 < s.size() ? tbl[(v >> 6) & 63] : '=';
                            out += i + 2 < s.size() ? tbl[v & 63] : '=';
                        }
                        c.Output("result", out);
                        return true;
                    }));
    r.Register(Make("string.base64_decode", "Base64 Decode", "String",
                    "RFC 4648 base64 decoding (lenient)",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        auto val = [](char ch) -> int {
                            if (ch >= 'A' && ch <= 'Z') return ch - 'A';
                            if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
                            if (ch >= '0' && ch <= '9') return ch - '0' + 52;
                            if (ch == '+') return 62;
                            if (ch == '/') return 63;
                            return -1;
                        };
                        const std::string s = c.Input("text").AsString();
                        std::string out;
                        int acc = 0, bits = 0;
                        for (char ch : s) {
                            const int v = val(ch);
                            if (v < 0) {
                                if (ch == '=') break;
                                continue;
                            }
                            acc = (acc << 6) | v;
                            bits += 6;
                            if (bits >= 8) {
                                bits -= 8;
                                out.push_back(static_cast<char>((acc >> bits) & 0xFF));
                            }
                        }
                        c.Output("result", out);
                        return true;
                    }));
    r.Register(Make("string.escape", "Escape", "String", "JSON-style escaping",
                    {In("text", VT::kString, Value(""))}, {Out("result", VT::kString)},
                    [](EvalContext& c) {
                        const std::string s = c.Input("text").AsString();
                        std::string out;
                        out.reserve(s.size() + 8);
                        for (unsigned char ch : s) {
                            switch (ch) {
                            case '"': out += "\\\""; break;
                            case '\\': out += "\\\\"; break;
                            case '\n': out += "\\n"; break;
                            case '\r': out += "\\r"; break;
                            case '\t': out += "\\t"; break;
                            default:
                                if (ch < 0x20) {
                                    char buf[8];
                                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                                    out += buf;
                                } else out.push_back(static_cast<char>(ch));
                            }
                        }
                        c.Output("result", out);
                        return true;
                    }));

    // ══ Array — processing helpers ═══════════════════════════════════════
    r.Register(Make("array.reverse", "Reverse", "Array", "Reverses the array",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        Value::Array a = c.Input("array").AsArray();
                        // range-v3 view pipeline, then materialize.
                        const auto reversed =
                            a | ranges::views::reverse;
                        Value::Array out(reversed.begin(), reversed.end());
                        c.Output("array", std::move(out));
                        return true;
                    }));
    r.Register(Make("array.sort", "Sort", "Array", "Sorts ascending or descending",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        Value::Array a = c.Input("array").AsArray();
                        const bool desc = c.Param("order").AsString() == "desc";
                        if (desc) {
                            ranges::sort(a, std::greater<double>());
                        } else {
                            ranges::sort(a, std::less<double>());
                        }
                        c.Output("array", std::move(a));
                        return true;
                    },
                    {{"order", Value("asc"), {"asc", "desc"}}}));
    r.Register(Make("array.sum", "Sum", "Array", "Sum of all elements",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("value", VT::kNumber)},
                    [](EvalContext& c) {
                        const auto a = c.Input("array").AsArray();
                        // NB: cblas_dasum sums |x| — wrong for signed sum;
                        // the plain loop auto-vectorizes well enough.
                        double s = 0.0;
                        for (double v : a) s += v;
                        c.Output("value", s);
                        return true;
                    }));
    r.Register(Make("array.product", "Product", "Array", "Product of all elements",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("value", VT::kNumber)},
                    [](EvalContext& c) {
                        const auto a = c.Input("array").AsArray();
                        double p = 1.0;
                        for (double v : a) p *= v;
                        c.Output("value", p);
                        return true;
                    }));
    r.Register(Make("array.slice", "Slice", "Array", "array[start .. start+count)",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        const auto a = c.Input("array").AsArray();
                        const long n = static_cast<long>(a.size());
                        long start = static_cast<long>(c.Param("start").AsNumber());
                        if (start < 0) start += n;
                        start = std::clamp(start, 0L, n);
                        long count = static_cast<long>(c.Param("count").AsNumber());
                        if (count < 0 || start + count > n) count = n - start;
                        count = std::max(0L, count);
                        Value::Array out(a.begin() + start, a.begin() + start + count);
                        c.Output("array", std::move(out));
                        return true;
                    },
                    {{"start", Value(0.0), {}}, {"count", Value(-1.0), {}}}));
    r.Register(Make("array.normalize", "Normalize", "Array",
                    "Scales so peak (or RMS) hits target dB",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        Value::Array a = c.Input("array").AsArray();
                        if (a.empty()) { c.Output("array", std::move(a)); return true; }
                        const int n = static_cast<int>(a.size());
                        double level = 0.0;
                        if (c.Param("mode").AsString() == "rms") {
                            const double nrm = NormL2(a.data(), static_cast<std::size_t>(n));
                            level = nrm / std::sqrt(static_cast<double>(n));
                        } else {
                            const int idx = IndexOfMaxAbs(a.data(), static_cast<std::size_t>(n));
                            level = idx > 0 ? std::fabs(a[static_cast<std::size_t>(idx - 1)]) : 0.0;
                        }
                        const double target = DbToLin(c.Param("targetDb").AsNumber());
                        const double g = level > 1e-9 ? target / level : 1.0;
                        ScaleInPlace(a.data(), static_cast<std::size_t>(n), g);
                        c.Output("array", std::move(a));
                        return true;
                    },
                    {{"mode", Value("peak"), {"peak", "rms"}},
                     {"targetDb", Value(-1.0), {}}}));
    r.Register(Make("array.cumsum", "Cumulative Sum", "Array",
                    "Running total of array elements",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        const auto a = c.Input("array").AsArray();
                        Value::Array out(a.size());
                        double acc = 0;
                        for (std::size_t i = 0; i < a.size(); ++i) { acc += a[i]; out[i] = acc; }
                        c.Output("array", std::move(out));
                        return true;
                    }));
    r.Register(Make("array.convolve", "Convolve", "Array", "signal * kernel (FIR)",
                    {In("signal", VT::kArray, Value(Value::Array{})),
                     In("kernel", VT::kArray, Value(Value::Array{1.0}))},
                    {Out("signal", VT::kArray)}, [](EvalContext& c) {
                        const auto s = c.Input("signal").AsArray();
                        const auto k = c.Input("kernel").AsArray();
                        if (k.empty()) { c.Output("signal", s); return true; }
                        Value::Array out;
                        // FFT path wins once N*K is large enough to amortize
                        // the transform; tiny kernels stay on the direct loop.
                        const bool use_fft =
                            k.size() >= 16 && s.size() >= 512 &&
                            static_cast<double>(s.size()) * k.size() >= 300000.0;
                        if (!use_fft || !ConvolveFft(s, k, out)) {
                            out.assign(s.size(), 0.0);
                            for (std::size_t i = 0; i < s.size(); ++i) {
                                double acc = 0;
                                for (std::size_t j = 0; j < k.size() && i + j < s.size(); ++j)
                                    acc += s[i + j] * k[j];
                                out[i] = acc;
                            }
                        }
                        c.Output("signal", std::move(out));
                        return true;
                    }));
    r.Register(Make("array.resample", "Resample", "Array", "Linear-interpolation resample",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        const auto a = c.Input("array").AsArray();
                        const double ratio = std::clamp(c.Param("ratio").AsNumber(), 0.01, 100.0);
                        const std::size_t out_n =
                            static_cast<std::size_t>(std::clamp(a.size() * ratio, 1.0, 1e8));
                        Value::Array out(out_n);
                        for (std::size_t i = 0; i < out_n; ++i) {
                            const double pos = static_cast<double>(i) / ratio;
                            const std::size_t i0 = static_cast<std::size_t>(pos);
                            const double f = pos - i0;
                            const double a0 = i0 < a.size() ? a[i0] : 0.0;
                            const double a1 = i0 + 1 < a.size() ? a[i0 + 1] : a0;
                            out[i] = a0 + (a1 - a0) * f;
                        }
                        c.Output("array", std::move(out));
                        return true;
                    },
                    {{"ratio", Value(1.0), {}}}));
    r.Register(Make("array.find", "Find", "Array", "Index of first value equal to target (-1)",
                    {In("array", VT::kArray, Value(Value::Array{})),
                     In("target", VT::kNumber, Value(0.0))},
                    {Out("index", VT::kNumber)}, [](EvalContext& c) {
                        const auto a = c.Input("array").AsArray();
                        const double t = c.Input("target").AsNumber();
                        for (std::size_t i = 0; i < a.size(); ++i) {
                            if (a[i] == t) { c.Output("index", static_cast<double>(i)); return true; }
                        }
                        c.Output("index", -1.0);
                        return true;
                    }));
    r.Register(Make("array.clip", "Clip", "Array", "Clamps every element to [lo, hi]",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        Value::Array a = c.Input("array").AsArray();
                        const double lo = c.Param("lo").AsNumber();
                        const double hi = c.Param("hi").AsNumber();
                        for (double& v : a) v = std::clamp(v, std::min(lo, hi), std::max(lo, hi));
                        c.Output("array", std::move(a));
                        return true;
                    },
                    {{"lo", Value(-1.0), {}}, {"hi", Value(1.0), {}}}));
    r.Register(Make("array.fade", "Fade", "Array", "Linear fade-in / fade-out",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        Value::Array a = c.Input("array").AsArray();
                        const auto n = a.size();
                        const std::size_t fi = static_cast<std::size_t>(std::clamp(
                            c.Param("fadeIn").AsNumber(), 0.0, static_cast<double>(n)));
                        const std::size_t fo = static_cast<std::size_t>(std::clamp(
                            c.Param("fadeOut").AsNumber(), 0.0, static_cast<double>(n)));
                        for (std::size_t i = 0; i < n; ++i) {
                            double g = 1.0;
                            if (fi > 0 && i < fi) g *= static_cast<double>(i) / fi;
                            if (fo > 0 && i + fo > n) {
                                g *= static_cast<double>(n - i) / fo;
                            }
                            a[i] *= g;
                        }
                        c.Output("array", std::move(a));
                        return true;
                    },
                    {{"fadeIn", Value(0.0), {}}, {"fadeOut", Value(0.0), {}}}));
    r.Register(Make("array.map", "Map Range", "Array",
                    "x -> (x - inLo) * (outHi - outLo) / (inHi - inLo) + outLo",
                    {In("array", VT::kArray, Value(Value::Array{}))}, {Out("array", VT::kArray)},
                    [](EvalContext& c) {
                        const auto in = c.Input("array").AsArray();
                        // Params are loop-invariant — hoist them out of the
                        // per-element transform (Param() is a map lookup).
                        const double lo = c.Param("inLo").AsNumber();
                        const double hi = c.Param("inHi").AsNumber();
                        const double olo = c.Param("outLo").AsNumber();
                        const double ohi = c.Param("outHi").AsNumber();
                        const double d = hi - lo;
                        const auto mapped = in | ranges::views::transform([&](double v) {
                            return d == 0.0 ? olo : olo + (v - lo) * (ohi - olo) / d;
                        });
                        Value::Array out(mapped.begin(), mapped.end());
                        c.Output("array", std::move(out));
                        return true;
                    },
                    {{"inLo", Value(0.0), {}}, {"inHi", Value(1.0), {}},
                     {"outLo", Value(0.0), {}}, {"outHi", Value(1.0), {}}}));

    // ══ Audio — block DSP ═════════════════════════════════════════════════
    auto audio_node = [&](std::string type, std::string title, std::string desc,
                          std::vector<PortSpec> inputs, std::vector<PortSpec> outputs,
                          EvaluateFn fn, std::vector<ParamSpec> params = {},
                          bool pure = true) {
        r.Register(Make(std::move(type), std::move(title), "Audio", std::move(desc),
                        std::move(inputs), std::move(outputs), std::move(fn),
                        std::move(params), pure));
    };

    audio_node("audio.biquad", "EQ / Filter", "RBJ biquad filter over the block",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   BiquadProcess(a, c.Param("type").AsString(), c.Param("frequency").AsNumber(),
                                 c.Param("q").AsNumber(), c.Param("gainDb").AsNumber(), sr);
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"type", Value("lowpass"),
                 {"lowpass", "highpass", "bandpass", "notch", "peaking", "lowshelf", "highshelf"}},
                {"frequency", Value(1000.0), {}}, {"q", Value(0.707), {}},
                {"gainDb", Value(0.0), {}}, {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.delay", "Delay", "Fractional delay + feedback + damping",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const auto n = a.size();
                   if (n == 0) { c.Output("signal", std::move(a)); return true; }
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double delay_s = std::clamp(c.Param("timeMs").AsNumber(), 0.1, 5000.0) / 1000.0;
                   const double fb = std::clamp(c.Param("feedback").AsNumber(), 0.0, 0.95);
                   const double damp = std::clamp(c.Param("damping").AsNumber(), 0.0, 1.0);
                   const double mix = std::clamp(c.Param("mix").AsNumber(), 0.0, 1.0);
                   DelayLine dl;
                   dl.Resize(static_cast<std::size_t>(delay_s * sr) + static_cast<std::size_t>(n) + 16);
                   double lp = 0.0;
                   for (std::size_t i = 0; i < n; ++i) {
                       const double delayed = dl.Read(delay_s * sr);
                       lp += (delayed - lp) * (1.0 - damp);
                       dl.Write(a[i] + lp * fb);
                       a[i] = a[i] * (1.0 - mix) + delayed * mix;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"timeMs", Value(250.0), {}}, {"feedback", Value(0.35), {}},
                {"damping", Value(0.5), {}}, {"mix", Value(0.35), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.compressor", "Compressor", "Envelope-follower compressor with makeup",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const auto n = a.size();
                   if (n == 0) { c.Output("signal", std::move(a)); return true; }
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double thr = DbToLin(c.Param("thresholdDb").AsNumber());
                   const double ratio = std::max(1.0, c.Param("ratio").AsNumber());
                   const double att = std::exp(-1.0 / (sr * std::max(0.1, c.Param("attackMs").AsNumber()) / 1000.0));
                   const double rel = std::exp(-1.0 / (sr * std::max(1.0, c.Param("releaseMs").AsNumber()) / 1000.0));
                   const double makeup = DbToLin(c.Param("makeupDb").AsNumber());
                   double env = 0.0, gain = 1.0;
                   for (double& s : a) {
                       const double ad = std::fabs(s);
                       env = ad > env ? att * env + (1 - att) * ad : rel * env + (1 - rel) * ad;
                       const double over = env / thr;
                       const double gr = over > 1.0 ? 1.0 - (1.0 - 1.0 / ratio) * (1.0 - 1.0 / over) : 1.0;
                       gain += (gr - gain) * 0.05;
                       s *= gain * makeup;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"thresholdDb", Value(-18.0), {}}, {"ratio", Value(4.0), {}},
                {"attackMs", Value(10.0), {}}, {"releaseMs", Value(100.0), {}},
                {"makeupDb", Value(0.0), {}}, {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.gate", "Noise Gate", "Expander-style gate",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const auto n = a.size();
                   if (n == 0) { c.Output("signal", std::move(a)); return true; }
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double thr = DbToLin(c.Param("thresholdDb").AsNumber());
                   const double floor = DbToLin(c.Param("floorDb").AsNumber());
                   const double att = std::exp(-1.0 / (sr * std::max(0.1, c.Param("attackMs").AsNumber()) / 1000.0));
                   const double rel = std::exp(-1.0 / (sr * std::max(1.0, c.Param("releaseMs").AsNumber()) / 1000.0));
                   double env = 0.0, gain = floor;
                   for (double& s : a) {
                       const double ad = std::fabs(s);
                       env = ad > env ? att * env + (1 - att) * ad : rel * env + (1 - rel) * ad;
                       const double target = env > thr ? 1.0 : floor;
                       gain += (target - gain) * (target > gain ? 1.0 - att : 1.0 - rel);
                       s *= gain;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"thresholdDb", Value(-40.0), {}}, {"floorDb", Value(-60.0), {}},
                {"attackMs", Value(2.0), {}}, {"releaseMs", Value(80.0), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.limiter", "Limiter", "Peak limiter with release",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double ceil_lin = DbToLin(c.Param("ceilingDb").AsNumber());
                   const double rel = std::exp(-1.0 / (sr * std::max(1.0, c.Param("releaseMs").AsNumber()) / 1000.0));
                   double gain = 1.0;
                   for (double& s : a) {
                       const double peak = std::fabs(s);
                       const double need = peak > ceil_lin ? ceil_lin / peak : 1.0;
                       gain = need < gain ? need : rel * gain + (1 - rel) * need;
                       s *= gain;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"ceilingDb", Value(-0.3), {}}, {"releaseMs", Value(50.0), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.noise", "Noise", "White / pink / brown noise generator",
               {}, {Out("signal", VT::kArray)}, [](EvalContext& c) {
                   const auto n = static_cast<std::size_t>(
                       std::clamp(c.Param("samples").AsNumber(), 1.0, 1e7));
                   const std::string type = c.Param("type").AsString();
                   const double amp = c.Param("amplitude").AsNumber();
                   const uint64_t seed = static_cast<uint64_t>(c.Param("seed").AsNumber());
                   std::mt19937_64 rng(seed != 0 ? seed : 0x9E3779B97F4A7C15ULL);
                   std::uniform_real_distribution<double> uni(-1.0, 1.0);
                   Value::Array out(n);
                   double b0 = 0, b1 = 0, b2 = 0, brown = 0;
                   for (std::size_t i = 0; i < n; ++i) {
                       const double w = uni(rng);
                       double s = w;
                       if (type == "pink") {
                           // Paul Kellet economical pink filter.
                           b0 = 0.99765 * b0 + w * 0.0990460;
                           b1 = 0.96300 * b1 + w * 0.2965164;
                           b2 = 0.57000 * b2 + w * 1.0526913;
                           s = (b0 + b1 + b2 + w * 0.1848) * 0.25;
                       } else if (type == "brown") {
                           brown = (brown + 0.02 * w) / 1.02;
                           s = brown * 3.5;
                       }
                       out[i] = std::clamp(s * amp, -1.0, 1.0);
                   }
                   c.Output("signal", std::move(out));
                   return true;
               },
               {{"type", Value("white"), {"white", "pink", "brown"}},
                {"samples", Value(4096.0), {}}, {"amplitude", Value(0.5), {}},
                {"seed", Value(1.0), {}}});

    audio_node("audio.lfo", "LFO", "Low-frequency oscillator signal",
               {}, {Out("signal", VT::kArray)}, [](EvalContext& c) {
                   const auto n = static_cast<std::size_t>(
                       std::clamp(c.Param("samples").AsNumber(), 1.0, 1e7));
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double rate = std::max(0.01, c.Param("rate").AsNumber());
                   const double depth = c.Param("depth").AsNumber();
                   const double phase = c.Param("phase").AsNumber();
                   const std::string shape = c.Param("shape").AsString();
                   Value::Array out(n);
                   if (shape == "sine") {
                       // sin(2*pi*p) with p = fmod(rate*i/sr + phase, 1) —
                       // sin is 2*pi-periodic so the fmod drops out and the
                       // argument stays linear: VML sin over the whole block.
                       FillSinRamp(out.data(), n,
                                   2.0 * kPi * rate / sr, 2.0 * kPi * phase);
                       const int ni = static_cast<int>(n);
                       if (ni > 0) ScaleInPlace(out.data(), static_cast<std::size_t>(ni), depth);
                   } else {
                       for (std::size_t i = 0; i < n; ++i) {
                           double p = std::fmod(rate * static_cast<double>(i) / sr + phase, 1.0);
                           if (p < 0) p += 1.0;
                           double v;
                           if (shape == "square") v = p < 0.5 ? 1.0 : -1.0;
                           else if (shape == "saw") v = 2.0 * p - 1.0;
                           else v = 1.0 - 4.0 * std::fabs(p - 0.5);  // triangle
                           out[i] = v * depth;
                       }
                   }
                   c.Output("signal", std::move(out));
                   return true;
               },
               {{"rate", Value(5.0), {}}, {"depth", Value(1.0), {}},
                {"phase", Value(0.0), {}}, {"shape", Value("sine"),
                 {"sine", "square", "saw", "triangle"}},
                {"samples", Value(4096.0), {}}, {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.tremolo", "Tremolo", "Amplitude modulation",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double rate = std::max(0.01, c.Param("rate").AsNumber());
                   const double depth = std::clamp(c.Param("depth").AsNumber(), 0.0, 1.0);
                   const double offset = 1.0 - depth;
                   const std::size_t n = a.size();
                   std::vector<double> lfo(n);
                   FillSinRamp(lfo.data(), n, 2.0 * kPi * rate / sr, 0.0);
                   for (std::size_t i = 0; i < n; ++i) {
                       a[i] *= offset + depth * 0.5 * (1.0 + lfo[i]);
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"rate", Value(4.0), {}}, {"depth", Value(0.8), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.ringmod", "Ring Mod", "Carrier multiplication",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double freq = std::max(0.1, c.Param("frequency").AsNumber());
                   const double mix = std::clamp(c.Param("mix").AsNumber(), 0.0, 1.0);
                   const std::size_t n = a.size();
                   std::vector<double> carrier(n);
                   FillSinRamp(carrier.data(), n, 2.0 * kPi * freq / sr, 0.0);
                   for (std::size_t i = 0; i < n; ++i) {
                       a[i] = a[i] * (1.0 - mix) + a[i] * carrier[i] * mix;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"frequency", Value(440.0), {}}, {"mix", Value(1.0), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.chorus", "Chorus", "Modulated delay + feedback",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   ModDelayProcess(a, c.Param("sampleRate").AsNumber(),
                                   c.Param("rate").AsNumber(), c.Param("depthMs").AsNumber(),
                                   c.Param("feedback").AsNumber(), c.Param("mix").AsNumber(), false);
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"rate", Value(1.2), {}}, {"depthMs", Value(4.0), {}},
                {"feedback", Value(0.2), {}}, {"mix", Value(0.5), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.flanger", "Flanger", "Fast modulated delay, inverted feedback",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   ModDelayProcess(a, c.Param("sampleRate").AsNumber(),
                                   c.Param("rate").AsNumber(), c.Param("depthMs").AsNumber(),
                                   c.Param("feedback").AsNumber(), c.Param("mix").AsNumber(), true);
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"rate", Value(0.4), {}}, {"depthMs", Value(2.0), {}},
                {"feedback", Value(0.6), {}}, {"mix", Value(0.6), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.vibrato", "Vibrato", "Pitch wobble via modulated delay",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double rate = std::max(0.1, c.Param("rate").AsNumber());
                   const double depth = c.Param("depthMs").AsNumber();
                   DelayLine dl;
                   dl.Resize(static_cast<std::size_t>(0.05 * sr) + a.size() + 16);
                   const double center = 0.012 * sr;
                   const double d = depth * 0.001 * sr;
                   const std::size_t n = a.size();
                   std::vector<double> lfo(n);
                   FillSinRamp(lfo.data(), n, 2.0 * kPi * rate / sr, 0.0);
                   for (std::size_t i = 0; i < n; ++i) {
                       const double delayed = dl.Read(center + d * lfo[i]);
                       dl.Write(a[i]);
                       a[i] = delayed;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"rate", Value(5.5), {}}, {"depthMs", Value(4.0), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.reverb", "Reverb", "Freeverb-style algorithmic reverb",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   ReverbProcess(a, c.Param("sampleRate").AsNumber(),
                                 c.Param("roomSize").AsNumber(), c.Param("damping").AsNumber(),
                                 c.Param("wet").AsNumber());
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"roomSize", Value(0.6), {}}, {"damping", Value(0.4), {}},
                {"wet", Value(0.33), {}}, {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.dcblock", "DC Block", "One-pole DC blocker",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const double r = std::clamp(c.Param("r").AsNumber(), 0.9, 0.9999);
                   double x1 = 0, y1 = 0;
                   for (double& s : a) {
                       const double y = s - x1 + r * y1;
                       x1 = s; y1 = y; s = y;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"r", Value(0.995), {}}});

    audio_node("audio.softclip", "Soft Clip", "tanh saturation",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const double drive = std::max(0.1, c.Param("drive").AsNumber());
                   const double mix = std::clamp(c.Param("mix").AsNumber(), 0.0, 1.0);
                   for (double& s : a) {
                       const double wet = std::tanh(s * drive) / std::tanh(drive);
                       s = s * (1.0 - mix) + wet * mix;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"drive", Value(2.0), {}}, {"mix", Value(0.5), {}}});

    audio_node("audio.bitcrush", "Bitcrush", "Sample-rate / bit-depth reduction",
               {In("signal", VT::kArray, Value(Value::Array{}))}, {Out("signal", VT::kArray)},
               [](EvalContext& c) {
                   Value::Array a = c.Input("signal").AsArray();
                   const double sr = std::max(1.0, c.Param("sampleRate").AsNumber());
                   const double down = std::clamp(c.Param("downsample").AsNumber(), 1.0, 1000.0);
                   const double step = std::max(1.0, c.Param("bits").AsNumber());
                   const double levels = std::pow(2.0, step);
                   const std::size_t hold = static_cast<std::size_t>(
                       std::max(1.0, sr / down));
                   double held = 0;
                   for (std::size_t i = 0; i < a.size(); ++i) {
                       if (i % hold == 0) held = a[i];
                       a[i] = std::round(held * levels) / levels;
                   }
                   c.Output("signal", std::move(a));
                   return true;
               },
               {{"downsample", Value(1.0), {}}, {"bits", Value(8.0), {}},
                {"sampleRate", Value(44100.0), {}}});

    audio_node("audio.pan", "Pan", "Constant-power mono->stereo balance (L/R outputs)",
               {In("signal", VT::kArray, Value(Value::Array{}))},
               {Out("left", VT::kArray), Out("right", VT::kArray)}, [](EvalContext& c) {
                   const auto a = c.Input("signal").AsArray();
                   const double pan = std::clamp(c.Param("pan").AsNumber(), -1.0, 1.0);
                   const double angle = (pan + 1.0) * kPi * 0.25;
                   const double gl = std::cos(angle) * std::sqrt(2.0);
                   const double gr = std::sin(angle) * std::sqrt(2.0);
                   Value::Array l(a.size()), rr(a.size());
                   for (std::size_t i = 0; i < a.size(); ++i) {
                       l[i] = a[i] * gl;
                       rr[i] = a[i] * gr;
                   }
                   c.Output("left", std::move(l));
                   c.Output("right", std::move(rr));
                   return true;
               },
               {{"pan", Value(0.0), {}}});

    audio_node("audio.merge_stereo", "Merge Stereo", "Interleaves L/R into one array [L0,R0,L1,R1,...]",
               {In("left", VT::kArray, Value(Value::Array{})),
                In("right", VT::kArray, Value(Value::Array{}))},
               {Out("signal", VT::kArray)}, [](EvalContext& c) {
                   const auto l = c.Input("left").AsArray();
                   const auto rr = c.Input("right").AsArray();
                   const std::size_t n = std::max(l.size(), rr.size());
                   Value::Array out(n * 2);
                   for (std::size_t i = 0; i < n; ++i) {
                       out[i * 2] = i < l.size() ? l[i] : 0.0;
                       out[i * 2 + 1] = i < rr.size() ? rr[i] : 0.0;
                   }
                   c.Output("signal", std::move(out));
                   return true;
               });

    audio_node("audio.split_stereo", "Split Stereo", "Deinterleaves [L0,R0,...] into L/R",
               {In("signal", VT::kArray, Value(Value::Array{}))},
               {Out("left", VT::kArray), Out("right", VT::kArray)}, [](EvalContext& c) {
                   const auto s = c.Input("signal").AsArray();
                   Value::Array l(s.size() / 2), rr(s.size() / 2);
                   for (std::size_t i = 0; i < rr.size(); ++i) {
                       l[i] = s[i * 2];
                       rr[i] = s[i * 2 + 1];
                   }
                   c.Output("left", std::move(l));
                   c.Output("right", std::move(rr));
                   return true;
               });

    // Utility for debugging DSP chains.
    audio_node("audio.silence", "Silence", "Generates a block of zeros",
               {}, {Out("signal", VT::kArray)}, [](EvalContext& c) {
                   const auto n = static_cast<std::size_t>(
                       std::clamp(c.Param("samples").AsNumber(), 0.0, 1e7));
                   c.Output("signal", Value::Array(n, 0.0));
                   return true;
               },
               {{"samples", Value(4096.0), {}}});
}

}  // namespace XER::NodeGraph

