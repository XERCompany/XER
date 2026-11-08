#include "javascript/events/event_router.h"

#include "javascript/common/text_normalizer.h"

#include <algorithm>

namespace XER::JavaScript::Events {

EventRouter::EventRouter(XER::JavaScript::Bridge::FunctionBridge* bridge)
    : bridge_(bridge) {}

std::uint64_t EventRouter::Subscribe(const std::string& topic, const std::string& handler_name) {
    if (bridge_ == nullptr) {
        return 0;
    }

    const std::string normalized_topic = XER::JavaScript::Common::TextNormalizer::NormalizeTopic(topic);
    auto& list = subscriptions_[normalized_topic];
    list.push_back(Subscription{.token = next_token_, .handler_name = handler_name});
    return next_token_++;
}

bool EventRouter::Unsubscribe(std::uint64_t token) {
    for (auto& [_, list] : subscriptions_) {
        const auto it = std::remove_if(list.begin(), list.end(), [token](const Subscription& item) {
            return item.token == token;
        });

        if (it != list.end()) {
            list.erase(it, list.end());
            return true;
        }
    }
    return false;
}

std::size_t EventRouter::Publish(const std::string& topic, const std::string& text) {
    if (bridge_ == nullptr) {
        return 0;
    }

    const std::string normalized_topic = XER::JavaScript::Common::TextNormalizer::NormalizeTopic(topic);
    std::size_t delivered = 0;

    auto publish_from = [&](const std::string& key) {
        auto* list = FindTopicSubscriptions(key);
        if (list == nullptr) {
            return;
        }

        for (const Subscription& sub : *list) {
            if (bridge_->Invoke(sub.handler_name, normalized_topic, text, sequence_)) {
                ++delivered;
            }
        }
    };

    publish_from(normalized_topic);
    if (normalized_topic != "*") {
        publish_from("*");
    }

    ++sequence_;
    return delivered;
}

std::vector<EventRouter::Subscription>* EventRouter::FindTopicSubscriptions(const std::string& topic) {
    auto it = subscriptions_.find(topic);
    if (it == subscriptions_.end()) {
        return nullptr;
    }
    return &it->second;
}

}  // namespace XER::JavaScript::Events
