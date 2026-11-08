#pragma once

#include "javascript/bridge/function_bridge.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::JavaScript::Events {

class EventRouter {
public:
    explicit EventRouter(XER::JavaScript::Bridge::FunctionBridge* bridge);

    std::uint64_t Subscribe(const std::string& topic, const std::string& handler_name);
    bool Unsubscribe(std::uint64_t token);
    std::size_t Publish(const std::string& topic, const std::string& text);

private:
    struct Subscription {
        std::uint64_t token = 0;
        std::string handler_name;
    };

    std::vector<Subscription>* FindTopicSubscriptions(const std::string& topic);

    XER::JavaScript::Bridge::FunctionBridge* bridge_;
    std::unordered_map<std::string, std::vector<Subscription>> subscriptions_;
    std::uint64_t next_token_ = 1;
    std::uint64_t sequence_ = 1;
};

}  // namespace XER::JavaScript::Events
