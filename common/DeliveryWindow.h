#pragma once

#include "ChatMessageTypes.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace chat::messages {
// Worker-confined state. The database remains authoritative; successful queries
// retire acknowledged entries. A failed query must never clear this window.
class DeliveryWindow {
  public:
    using Clock = std::chrono::steady_clock;
    using Key = std::pair<int, std::string>;
    static constexpr int Capacity = 32;
    void reconcile(const std::vector<TextMessage>& pending) {
        std::set<Key> keys;
        for (const auto& message : pending) keys.insert(key(message));
        for (auto it = attempts_.begin(); it != attempts_.end();)
            if (!keys.count(it->first)) it = attempts_.erase(it);
            else ++it;
    }
    bool due(const TextMessage& message, Clock::time_point now) const {
        auto it = attempts_.find(key(message));
        return it == attempts_.end() || now >= it->second.next;
    }
    // Call only after the bounded socket queue accepts the message.
    bool sent(const TextMessage& message, Clock::time_point now) {
        auto& attempt = attempts_[key(message)];
        bool retry = attempt.count != 0;
        attempt.next = now + std::chrono::seconds(5 * (1 << std::min(attempt.count, 3U)));
        attempt.count = std::min(attempt.count + 1, 4U);
        return retry;
    }
    std::size_t size() const { return attempts_.size(); }

  private:
    static Key key(const TextMessage& message) { return {message.sender_uid, message.client_message_id}; }
    struct Attempt { unsigned count = 0; Clock::time_point next; };
    std::map<Key, Attempt> attempts_;
};
} // namespace chat::messages
