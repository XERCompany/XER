#include "flow_script_env.h"


namespace XER {
FlowScriptEnv::FlowScriptEnv(FlowScriptEnvData* data) {
	if (data == nullptr) {
		data_ = std::make_unique<FlowScriptEnvData>();
		return;
	}

	data_ = std::make_unique<FlowScriptEnvData>(*data);
}

const FlowScriptEnvData& FlowScriptEnv::data() const {
	return *data_;
}
} // namespace XER