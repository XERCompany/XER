#pragma once

#include <string>
#include <vector>

namespace XER::Audio::AnalysisAI {

struct MusicRemarkMetrics {
	float bpm = 0.0f;
	float beat_strength = 0.0f;
	float pitch_confidence = 0.0f;
	float loudness_integrated_lufs = 0.0f;
	float true_peak_dbfs = 0.0f;
	float duration_seconds = 0.0f;
	std::string pitch_note;
};

struct MusicRemarkOptions {
	std::string title;
	std::string artist;
	std::string target_mood;
	float sample_intensity = 0.45f;
	bool prefer_instrumental = false;
	bool prefer_acapella = false;
	float vocal_move_beats = 0.0f;
	float drums_move_beats = 0.0f;
	float vocal_stretch_ratio = 1.0f;
	std::string openvino_model_path;
};

struct MusicRemarkSampleSlot {
	std::string kind;
	std::string label;
	std::string search_tag;
	float beat_offset = 0.0f;
	float confidence = 0.0f;
	std::string effect_anchor;
};

struct MusicRemarkStemAction {
	std::string stem;
	std::string operation;
	float amount = 0.0f;
	float beat_offset = 0.0f;
	float stretch_ratio = 1.0f;
	float confidence = 0.0f;
	std::string reason;
	std::vector<std::string> effects;
};

struct MusicRemarkEffectLayer {
	std::string target;
	std::vector<std::string> effects;
	float mix = 1.0f;
};

struct MusicRemarkDjSection {
	std::string name;
	std::string purpose;
	float start_beat = 0.0f;
	float length_beats = 0.0f;
	float energy = 0.0f;
	std::vector<std::string> operations;
	std::vector<std::string> effects;
};

struct MusicRemarkTransitionAction {
	std::string kind;
	std::string label;
	std::string from_section;
	std::string to_section;
	float beat_offset = 0.0f;
	float length_beats = 0.0f;
	float confidence = 0.0f;
	std::vector<std::string> effects;
};

struct MusicRemarkMixPlan {
	bool openvino_available = false;
	std::string backend;
	std::string model_hint;
	bool model_found = false;
	std::string model_source;
	std::vector<std::string> model_suggestions;
	float score = 0.0f;
	std::string grade;
	std::vector<std::string> effects;
	std::vector<std::string> reasons;
	std::vector<MusicRemarkSampleSlot> sample_slots;
	std::vector<MusicRemarkStemAction> stem_actions;
	std::vector<MusicRemarkEffectLayer> effect_layers;
	std::vector<MusicRemarkDjSection> dj_sections;
	std::vector<MusicRemarkTransitionAction> transition_actions;
};

bool IsOpenVinoRuntimeAvailable();
MusicRemarkMixPlan BuildOpenVinoMusicRemarkMixPlan(
	const MusicRemarkMetrics& metrics,
	const MusicRemarkOptions& options = MusicRemarkOptions{});

}  // namespace XER::Audio::AnalysisAI