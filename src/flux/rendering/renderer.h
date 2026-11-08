#pragma once

#include <string>

#include "flux/rendering/render_context.h"

namespace XER::Flux::Rendering {

class Renderer {
public:
	std::string RenderPlainText(const RenderContext& context) const;
	std::string RenderAnsi(const RenderContext& context) const;
};

}  // namespace XER::Flux::Rendering
