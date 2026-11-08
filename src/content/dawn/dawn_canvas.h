#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <dawn/webgpu_cpp.h>

#include "dawn_context.h"

namespace XER::Content::Dawn {

struct Color {
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 1.0f;
};

// Offscreen render target on a DawnContext. Each RenderFrame call
// clears, runs the caller's draw callback inside a render pass,
// submits, and optionally reads back RGBA8 pixels.
class DawnCanvas {
public:
	using DrawFn = std::function<void(wgpu::RenderPassEncoder& pass)>;

	explicit DawnCanvas(DawnContext* context);
	~DawnCanvas();

	DawnCanvas(const DawnCanvas&) = delete;
	DawnCanvas& operator=(const DawnCanvas&) = delete;

	bool Initialize(uint32_t width, uint32_t height,
			std::string* error_out = nullptr);
	void Resize(uint32_t width, uint32_t height);  // applied lazily
	uint32_t width() const { return width_; }
	uint32_t height() const { return height_; }

	bool RenderFrame(const Color& clear_color, const DrawFn& draw_fn,
			 std::vector<uint8_t>* rgba_out = nullptr,
			 std::string* error_out = nullptr);

	// Renders a fullscreen gradient triangle through the built-in
	// pipeline and returns RGBA8 pixels. Useful as a smoke test and
	// as a minimal working example for apps.
	bool RenderTestPattern(std::vector<uint8_t>* rgba_out,
			       std::string* error_out = nullptr);

private:
	bool EnsureTargets(std::string* error_out);
	bool EnsureColorPipeline(std::string* error_out);
	bool ReadbackRGBA(std::vector<uint8_t>* rgba_out, std::string* error_out);

	DawnContext* context_ = nullptr;
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	bool targets_dirty_ = true;
	bool pipeline_ready_ = false;

	wgpu::Texture color_tex_;
	wgpu::Texture depth_tex_;
	wgpu::TextureView color_view_;
	wgpu::TextureView depth_view_;

	// Built-in solid-color fullscreen pipeline
	wgpu::ShaderModule shader_module_;
	wgpu::BindGroupLayout bgl_;
	wgpu::PipelineLayout pipeline_layout_;
	wgpu::RenderPipeline pipeline_;
	wgpu::Buffer uniform_buffer_;
	wgpu::BindGroup bind_group_;

	// Readback staging
	wgpu::Buffer readback_buffer_;
	size_t readback_row_pitch_ = 0;
};

}  // namespace XER::Content::Dawn
