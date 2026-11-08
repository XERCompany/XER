#include "dawn_canvas.h"

#include <algorithm>
#include <cstring>

namespace XER::Content::Dawn {
namespace {

constexpr uint32_t kBytesPerPixel = 4;
constexpr uint64_t kMapTimeoutNs = 5'000'000'000ull;

size_t Align256(size_t v) { return (v + 255) & ~size_t(255); }

const char kFullscreenShaderWgsl[] = R"wgsl(
struct Params {
    color : vec4<f32>,
};

@group(0) @binding(0) var<uniform> params : Params;

struct VsOut {
    @builtin(position) pos : vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vi : u32) -> VsOut {
    // Fullscreen triangle: 3 vertices covering the whole target.
    var pos = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -3.0),
        vec2<f32>( 3.0,  1.0),
        vec2<f32>(-1.0,  1.0),
    );
    var out : VsOut;
    out.pos = vec4<f32>(pos[vi], 0.0, 1.0);
    return out;
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return params.color;
}
)wgsl";

}  // namespace

DawnCanvas::DawnCanvas(DawnContext* context) : context_(context) {}

DawnCanvas::~DawnCanvas() {
	// Handles are ref-counted; explicit reset not required but keeps
	// ordering obvious for devices that tear down on last release.
	bind_group_ = wgpu::BindGroup{};
	uniform_buffer_ = wgpu::Buffer{};
	pipeline_ = wgpu::RenderPipeline{};
	pipeline_layout_ = wgpu::PipelineLayout{};
	bgl_ = wgpu::BindGroupLayout{};
	shader_module_ = wgpu::ShaderModule{};
	readback_buffer_ = wgpu::Buffer{};
	color_view_ = wgpu::TextureView{};
	depth_view_ = wgpu::TextureView{};
	color_tex_ = wgpu::Texture{};
	depth_tex_ = wgpu::Texture{};
}

bool DawnCanvas::Initialize(uint32_t width, uint32_t height,
			    std::string* error_out) {
	if (!context_ || !context_->is_ready()) {
		if (error_out) *error_out = "DawnContext not ready";
		return false;
	}
	width_ = width;
	height_ = height;
	targets_dirty_ = true;
	pipeline_ready_ = false;
	return true;
}

void DawnCanvas::Resize(uint32_t width, uint32_t height) {
	if (width == width_ && height == height_) return;
	width_ = width;
	height_ = height;
	targets_dirty_ = true;
}

bool DawnCanvas::EnsureTargets(std::string* error_out) {
	if (!targets_dirty_ && color_tex_ && depth_tex_) return true;
	if (width_ == 0 || height_ == 0) {
		if (error_out) *error_out = "canvas has zero size";
		return false;
	}
	wgpu::Device& device = context_->device();

	wgpu::TextureDescriptor color_desc{};
	color_desc.size = {width_, height_, 1};
	color_desc.format = wgpu::TextureFormat::RGBA8Unorm;
	color_desc.usage = wgpu::TextureUsage::RenderAttachment |
			   wgpu::TextureUsage::CopySrc;
	color_desc.mipLevelCount = 1;
	color_desc.sampleCount = 1;
	color_tex_ = device.CreateTexture(&color_desc);
	if (!color_tex_) {
		if (error_out) *error_out = "CreateTexture(color) failed";
		return false;
	}
	color_view_ = color_tex_.CreateView();

	wgpu::TextureDescriptor depth_desc = color_desc;
	depth_desc.format = wgpu::TextureFormat::Depth24Plus;
	depth_desc.usage = wgpu::TextureUsage::RenderAttachment;
	depth_tex_ = device.CreateTexture(&depth_desc);
	if (!depth_tex_) {
		if (error_out) *error_out = "CreateTexture(depth) failed";
		return false;
	}
	depth_view_ = depth_tex_.CreateView();

	// Readback buffer sized for padded rows.
	readback_row_pitch_ = Align256(static_cast<size_t>(width_) * kBytesPerPixel);
	const size_t buffer_size = readback_row_pitch_ * height_;
	wgpu::BufferDescriptor buf_desc{};
	buf_desc.size = buffer_size;
	buf_desc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
	readback_buffer_ = device.CreateBuffer(&buf_desc);
	if (!readback_buffer_) {
		if (error_out) *error_out = "CreateBuffer(readback) failed";
		return false;
	}
	targets_dirty_ = false;
	return true;
}

bool DawnCanvas::EnsureColorPipeline(std::string* error_out) {
	if (pipeline_ready_) return true;
	wgpu::Device& device = context_->device();

	wgpu::ShaderSourceWGSL wgsl_source;
	wgsl_source.code = wgpu::StringView{kFullscreenShaderWgsl};
	wgpu::ShaderModuleDescriptor shader_desc{};
	shader_desc.nextInChain = &wgsl_source;
	shader_module_ = device.CreateShaderModule(&shader_desc);
	if (!shader_module_) {
		if (error_out) *error_out = "CreateShaderModule failed";
		return false;
	}

	wgpu::BindGroupLayoutEntry bgl_entry{};
	bgl_entry.binding = 0;
	bgl_entry.visibility = wgpu::ShaderStage::Fragment;
	bgl_entry.buffer.type = wgpu::BufferBindingType::Uniform;
	bgl_entry.buffer.hasDynamicOffset = false;
	bgl_entry.buffer.minBindingSize = 0;

	wgpu::BindGroupLayoutDescriptor bgl_desc{};
	bgl_desc.entryCount = 1;
	bgl_desc.entries = &bgl_entry;
	bgl_ = device.CreateBindGroupLayout(&bgl_desc);
	if (!bgl_) {
		if (error_out) *error_out = "CreateBindGroupLayout failed";
		return false;
	}

	wgpu::PipelineLayoutDescriptor pl_desc{};
	pl_desc.bindGroupLayoutCount = 1;
	pl_desc.bindGroupLayouts = &bgl_;
	pipeline_layout_ = device.CreatePipelineLayout(&pl_desc);
	if (!pipeline_layout_) {
		if (error_out) *error_out = "CreatePipelineLayout failed";
		return false;
	}

	wgpu::BufferDescriptor ubuf_desc{};
	ubuf_desc.size = 16;  // vec4<f32>
	ubuf_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	uniform_buffer_ = device.CreateBuffer(&ubuf_desc);
	if (!uniform_buffer_) {
		if (error_out) *error_out = "CreateBuffer(uniform) failed";
		return false;
	}

	wgpu::BindGroupEntry bg_entry{};
	bg_entry.binding = 0;
	bg_entry.buffer = uniform_buffer_;
	bg_entry.offset = 0;
	bg_entry.size = 16;
	wgpu::BindGroupDescriptor bg_desc{};
	bg_desc.layout = bgl_;
	bg_desc.entryCount = 1;
	bg_desc.entries = &bg_entry;
	bind_group_ = device.CreateBindGroup(&bg_desc);
	if (!bind_group_) {
		if (error_out) *error_out = "CreateBindGroup failed";
		return false;
	}

	wgpu::VertexState vertex_state{};
	vertex_state.module = shader_module_;
	vertex_state.entryPoint = wgpu::StringView{"vs_main"};

	wgpu::FragmentState fragment_state{};
	fragment_state.module = shader_module_;
	fragment_state.entryPoint = wgpu::StringView{"fs_main"};
	wgpu::BlendState blend{};
	blend.color.operation = wgpu::BlendOperation::Add;
	blend.color.srcFactor = wgpu::BlendFactor::One;
	blend.color.dstFactor = wgpu::BlendFactor::Zero;
	blend.alpha.operation = wgpu::BlendOperation::Add;
	blend.alpha.srcFactor = wgpu::BlendFactor::One;
	blend.alpha.dstFactor = wgpu::BlendFactor::Zero;
	wgpu::ColorTargetState target_state{};
	target_state.format = wgpu::TextureFormat::RGBA8Unorm;
	target_state.blend = &blend;
	target_state.writeMask = wgpu::ColorWriteMask::All;
	fragment_state.targetCount = 1;
	fragment_state.targets = &target_state;

	wgpu::PrimitiveState primitive{};
	primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	primitive.frontFace = wgpu::FrontFace::CCW;
	primitive.cullMode = wgpu::CullMode::None;

	wgpu::DepthStencilState depth_state{};
	depth_state.format = wgpu::TextureFormat::Depth24Plus;
	depth_state.depthWriteEnabled = true;
	depth_state.depthCompare = wgpu::CompareFunction::Less;

	wgpu::MultisampleState msaa{};
	msaa.count = 1;

	wgpu::RenderPipelineDescriptor pipe_desc{};
	pipe_desc.layout = pipeline_layout_;
	pipe_desc.vertex = vertex_state;
	pipe_desc.fragment = &fragment_state;
	pipe_desc.primitive = primitive;
	pipe_desc.depthStencil = &depth_state;
	pipe_desc.multisample = msaa;

	pipeline_ = device.CreateRenderPipeline(&pipe_desc);
	if (!pipeline_) {
		if (error_out) *error_out = "CreateRenderPipeline failed";
		return false;
	}
	pipeline_ready_ = true;
	return true;
}

bool DawnCanvas::ReadbackRGBA(std::vector<uint8_t>* rgba_out,
			      std::string* error_out) {
	if (!readback_buffer_) {
		if (error_out) *error_out = "no readback buffer";
		return false;
	}
	rgba_out->assign(static_cast<size_t>(width_) * height_ * kBytesPerPixel, 0);

	wgpu::Buffer& buf = readback_buffer_;
	wgpu::MapAsyncStatus status = wgpu::MapAsyncStatus::Error;
	const size_t map_size = readback_row_pitch_ * height_;
	wgpu::Future fut = buf.MapAsync(
	    wgpu::MapMode::Read, 0, map_size,
	    wgpu::CallbackMode::WaitAnyOnly,
	    [&status](wgpu::MapAsyncStatus s, wgpu::StringView) { status = s; });
	if (!context_->WaitFuture(fut, kMapTimeoutNs, error_out)) {
		return false;
	}
	if (status != wgpu::MapAsyncStatus::Success) {
		if (error_out) {
			*error_out = "MapAsync failed (status=" +
				     std::to_string(static_cast<int>(status)) + ")";
		}
		return false;
	}

	const uint8_t* mapped = static_cast<const uint8_t*>(
	    buf.GetConstMappedRange(0, map_size));
	if (!mapped) {
		if (error_out) *error_out = "GetMappedRange returned null";
		return false;
	}
	const size_t tight_row = static_cast<size_t>(width_) * kBytesPerPixel;
	for (uint32_t y = 0; y < height_; ++y) {
		std::memcpy(rgba_out->data() + static_cast<size_t>(y) * tight_row,
			    mapped + static_cast<size_t>(y) * readback_row_pitch_,
			    tight_row);
	}
	buf.Unmap();
	return true;
}

bool DawnCanvas::RenderFrame(const Color& clear_color, const DrawFn& draw_fn,
			     std::vector<uint8_t>* rgba_out,
			     std::string* error_out) {
	if (!context_ || !context_->is_ready()) {
		if (error_out) *error_out = "DawnContext not ready";
		return false;
	}
	if (!EnsureTargets(error_out)) return false;
	wgpu::Device& device = context_->device();

	wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
	if (!encoder) {
		if (error_out) *error_out = "CreateCommandEncoder failed";
		return false;
	}

	wgpu::RenderPassColorAttachment color_att{};
	color_att.view = color_view_;
	color_att.loadOp = wgpu::LoadOp::Clear;
	color_att.storeOp = wgpu::StoreOp::Store;
	color_att.clearValue = {clear_color.r, clear_color.g, clear_color.b,
				clear_color.a};

	wgpu::RenderPassDepthStencilAttachment depth_att{};
	depth_att.view = depth_view_;
	depth_att.depthLoadOp = wgpu::LoadOp::Clear;
	depth_att.depthStoreOp = wgpu::StoreOp::Store;
	depth_att.depthClearValue = 1.0f;

	wgpu::RenderPassDescriptor pass_desc{};
	pass_desc.colorAttachmentCount = 1;
	pass_desc.colorAttachments = &color_att;
	pass_desc.depthStencilAttachment = &depth_att;

	{
		wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&pass_desc);
		if (!pass) {
			if (error_out) *error_out = "BeginRenderPass failed";
			return false;
		}
		if (draw_fn) draw_fn(pass);
		pass.End();
	}

	if (rgba_out) {
		wgpu::TexelCopyTextureInfo copy_src{};
		copy_src.texture = color_tex_;
		copy_src.aspect = wgpu::TextureAspect::All;
		wgpu::TexelCopyBufferInfo copy_dst{};
		copy_dst.buffer = readback_buffer_;
		copy_dst.layout.bytesPerRow =
		    static_cast<uint32_t>(readback_row_pitch_);
		copy_dst.layout.rowsPerImage = height_;
		wgpu::Extent3D copy_size{width_, height_, 1};
		encoder.CopyTextureToBuffer(&copy_src, &copy_dst, &copy_size);
	}

	wgpu::CommandBuffer commands = encoder.Finish();
	if (!commands) {
		if (error_out) *error_out = "encoder.Finish failed";
		return false;
	}
	context_->queue().Submit(1, &commands);

	if (rgba_out && !ReadbackRGBA(rgba_out, error_out)) return false;
	return true;
}

bool DawnCanvas::RenderTestPattern(std::vector<uint8_t>* rgba_out,
				   std::string* error_out) {
	if (!EnsureColorPipeline(error_out)) return false;

	const float color[4] = {0.2f, 0.6f, 0.9f, 1.0f};
	context_->queue().WriteBuffer(uniform_buffer_, 0, color, sizeof(color));

	return RenderFrame(Color{0.05f, 0.05f, 0.08f, 1.0f},
			   [this](wgpu::RenderPassEncoder& pass) {
			   pass.SetPipeline(pipeline_);
			   pass.SetBindGroup(0, bind_group_);
			   pass.Draw(3);
			   },
			   rgba_out, error_out);
}

}  // namespace XER::Content::Dawn
