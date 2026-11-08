#pragma once

#include "javascript/events/event_router.h"

#include <cstdint>
#include <string>
#include <vector>

namespace XER::JavaScript::Pipeline {

class PipelineExecutor {
public:
    explicit PipelineExecutor(XER::JavaScript::Events::EventRouter* router);

    std::size_t PublishRaw(const std::string& topic, const std::string& text);
    std::size_t PublishCompressed(const std::string& topic, const std::vector<std::uint8_t>& compressed);

private:
    XER::JavaScript::Events::EventRouter* router_;
};

}  // namespace XER::JavaScript::Pipeline
