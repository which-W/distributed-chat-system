#pragma once
#include "ChatMessageTypes.h"
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace chat::messages {
// Domain acceptance has no socket, singleton, Redis or RPC dependency.
class TextMessageService {
  public:
    enum class Result { Accepted, InvalidMessage, InvalidRecipient, Unavailable };
    using Friendship = std::function<std::optional<bool>(int, int)>;
    using Persist = std::function<bool(const std::vector<TextMessage>&)>;
    TextMessageService(Friendship friendship, Persist persist)
        : friendship_(std::move(friendship)), persist_(std::move(persist)) {}
    Result accept(int sender, int receiver, const std::vector<TextMessage>& messages) const {
        if (sender <= 0 || receiver <= 0 || sender == receiver)
            return Result::InvalidRecipient;
        if (messages.empty() || messages.size() > 50)
            return Result::InvalidMessage;
        std::size_t bytes = 0;
        for (const auto& message : messages) {
            if (message.sender_uid != sender || message.receiver_uid != receiver ||
                message.client_message_id.empty() || message.client_message_id.size() > 128 ||
                message.content.empty() || message.content.size() > 2048)
                return Result::InvalidMessage;
            bytes += message.client_message_id.size() + message.content.size();
            if (bytes > 8192)
                return Result::InvalidMessage;
        }
        try {
            const auto allowed = friendship_(sender, receiver);
            if (!allowed)
                return Result::Unavailable;
            if (!*allowed)
                return Result::InvalidRecipient;
            return persist_(messages) ? Result::Accepted : Result::Unavailable;
        } catch (...) {
            return Result::Unavailable;
        }
    }

  private:
    Friendship friendship_;
    Persist persist_;
};
} // namespace chat::messages
