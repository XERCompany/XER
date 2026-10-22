#ifndef XER_AUDIO_CONFIG_HELPER_H
#define XER_AUDIO_CONFIG_HELPER_H

#include "app_command.h"
#include "json_config_helper.h"
#include <string>

namespace XER {

// Helper for applying audio-specific JSON configuration
class AudioConfigHelper {
public:
    // Apply audio configuration from JSON file to parsed command
    static bool ApplyAudioConfigFile(
        AppCommand::Parsed* parsed,
        std::string* error_out);

private:
    // Get effects list from JSON
    static std::vector<std::string> GetJsonConfigEffects(const Json::Value& root);
};

} // namespace XER

#endif // XER_AUDIO_CONFIG_HELPER_H
