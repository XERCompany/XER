
#include <map>
#include <memory>
#include <string>


namespace XER {
struct FlowScriptEnvData
{
    std::map<std::string, std::string> env;
};


class FlowScriptEnv {
public:
    FlowScriptEnv(FlowScriptEnvData* data);

    const FlowScriptEnvData& data() const;

private:
    std::unique_ptr<FlowScriptEnvData> data_;
};
} // namespace XER