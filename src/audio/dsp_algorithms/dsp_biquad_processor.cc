#include "dsp_biquad_processor.h"

#include <cmath>

namespace XER::Engine::Audio::DSP {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

BiquadProcessor::BiquadProcessor()
	: type_(BiquadType::LowPass),
	  sample_rate_(44100.0f),
	  frequency_(1000.0f),
	  q_(0.707f),
	  b0_(1.0f),
	  b1_(0.0f),
	  b2_(0.0f),
	  a1_(0.0f),
	  a2_(0.0f),
	  x1_(0.0f),
	  x2_(0.0f),
	  y1_(0.0f),
	  y2_(0.0f) {
	perf_counter_.Enable();
}

BiquadProcessor::BiquadProcessor(const BiquadProcessor& other)
	: type_(other.type_),
	  sample_rate_(other.sample_rate_),
	  frequency_(other.frequency_),
	  q_(other.q_),
	  b0_(other.b0_),
	  b1_(other.b1_),
	  b2_(other.b2_),
	  a1_(other.a1_),
	  a2_(other.a2_),
	  x1_(other.x1_),
	  x2_(other.x2_),
	  y1_(other.y1_),
	  y2_(other.y2_) {
	perf_counter_.Enable();
}

BiquadProcessor::BiquadProcessor(BiquadProcessor&& other) noexcept
	: type_(other.type_),
	  sample_rate_(other.sample_rate_),
	  frequency_(other.frequency_),
	  q_(other.q_),
	  b0_(other.b0_),
	  b1_(other.b1_),
	  b2_(other.b2_),
	  a1_(other.a1_),
	  a2_(other.a2_),
	  x1_(other.x1_),
	  x2_(other.x2_),
	  y1_(other.y1_),
	  y2_(other.y2_) {
	perf_counter_.Enable();
}

BiquadProcessor::~BiquadProcessor() = default;

BiquadProcessor& BiquadProcessor::operator=(const BiquadProcessor& other) {
	if (this == &other) {
		return *this;
	}

	type_ = other.type_;
	sample_rate_ = other.sample_rate_;
	frequency_ = other.frequency_;
	q_ = other.q_;
	b0_ = other.b0_;
	b1_ = other.b1_;
	b2_ = other.b2_;
	a1_ = other.a1_;
	a2_ = other.a2_;
	x1_ = other.x1_;
	x2_ = other.x2_;
	y1_ = other.y1_;
	y2_ = other.y2_;
	perf_counter_.ResetAll();
	perf_counter_.Enable();
	return *this;
}

BiquadProcessor& BiquadProcessor::operator=(BiquadProcessor&& other) noexcept {
	if (this == &other) {
		return *this;
	}

	type_ = other.type_;
	sample_rate_ = other.sample_rate_;
	frequency_ = other.frequency_;
	q_ = other.q_;
	b0_ = other.b0_;
	b1_ = other.b1_;
	b2_ = other.b2_;
	a1_ = other.a1_;
	a2_ = other.a2_;
	x1_ = other.x1_;
	x2_ = other.x2_;
	y1_ = other.y1_;
	y2_ = other.y2_;
	perf_counter_.ResetAll();
	perf_counter_.Enable();
	return *this;
}

bool BiquadProcessor::Configure(BiquadType type, float sample_rate, float frequency, float q) {
	if (sample_rate <= 0.0f || frequency <= 0.0f || q <= 0.0f) {
		return false;
	}

	type_ = type;
	sample_rate_ = sample_rate;
	frequency_ = frequency;
	q_ = q;
	return ComputeCoefficients();
}

float BiquadProcessor::ProcessSample(float sample) {
	const float y0 = b0_ * sample + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
	x2_ = x1_;
	x1_ = sample;
	y2_ = y1_;
	y1_ = y0;
	return y0;
}

void BiquadProcessor::Reset() {
	x1_ = 0.0f;
	x2_ = 0.0f;
	y1_ = 0.0f;
	y2_ = 0.0f;
}

std::string BiquadProcessor::GetReport() const {
	return "Biquad: sr=" + std::to_string(sample_rate_) +
		", f=" + std::to_string(frequency_) +
		", q=" + std::to_string(q_);
}

bool BiquadProcessor::ComputeCoefficients() {
	const float w0 = 2.0f * kPi * frequency_ / sample_rate_;
	const float cosw = std::cos(w0);
	const float sinw = std::sin(w0);
	const float alpha = sinw / (2.0f * q_);

	float nb0 = 0.0f;
	float nb1 = 0.0f;
	float nb2 = 0.0f;
	float na0 = 1.0f + alpha;
	float na1 = -2.0f * cosw;
	float na2 = 1.0f - alpha;

	switch (type_) {
		case BiquadType::LowPass:
			nb0 = (1.0f - cosw) * 0.5f;
			nb1 = 1.0f - cosw;
			nb2 = (1.0f - cosw) * 0.5f;
			break;
		case BiquadType::HighPass:
			nb0 = (1.0f + cosw) * 0.5f;
			nb1 = -(1.0f + cosw);
			nb2 = (1.0f + cosw) * 0.5f;
			break;
		case BiquadType::BandPass:
			nb0 = sinw * 0.5f;
			nb1 = 0.0f;
			nb2 = -sinw * 0.5f;
			break;
		case BiquadType::Notch:
			nb0 = 1.0f;
			nb1 = -2.0f * cosw;
			nb2 = 1.0f;
			break;
	}

	if (std::abs(na0) < 1e-9f) {
		return false;
	}

	b0_ = nb0 / na0;
	b1_ = nb1 / na0;
	b2_ = nb2 / na0;
	a1_ = na1 / na0;
	a2_ = na2 / na0;
	return true;
}

}  // namespace XER::Engine::Audio::DSP
