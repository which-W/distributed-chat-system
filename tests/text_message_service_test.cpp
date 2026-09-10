#include "TextMessageService.h"
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() try {
    using Service = chat::messages::TextMessageService;
    using Result = Service::Result;
    int lookups = 0, commits = 0;
    std::optional<bool> friends = true;
    bool commit_ok = true, throw_commit = false;
    Service service(
        [&](int sender, int receiver) {
            ++lookups;
            check(sender == 1 && receiver == 2, "wrong authorization identities");
            return friends;
        },
        [&](const auto& batch) {
            ++commits;
            if (throw_commit)
                throw std::runtime_error("database timeout");
            check(batch.front().client_message_id == "stable-id", "ID changed during acceptance");
            return commit_ok;
        });
    std::vector<chat::messages::TextMessage> messages{{"stable-id", 1, 2, "hello"}};
    check(service.accept(1, 2, messages) == Result::Accepted && commits == 1,
          "valid commit not accepted");
    friends = false;
    check(service.accept(1, 2, messages) == Result::InvalidRecipient && commits == 1,
          "unauthorized message persisted");
    friends = std::nullopt;
    check(service.accept(1, 2, messages) == Result::Unavailable && commits == 1,
          "DB outage confused with no friendship");
    friends = true;
    commit_ok = false;
    check(service.accept(1, 2, messages) == Result::Unavailable, "failed commit accepted");
    throw_commit = true;
    check(service.accept(1, 2, messages) == Result::Unavailable, "dependency exception escaped");
    const auto calls = lookups;
    messages.front().sender_uid = 3;
    check(service.accept(1, 2, messages) == Result::InvalidMessage && lookups == calls,
          "spoofed identity reached dependency");
    messages.front().sender_uid = 1;
    messages.front().content = std::string(2049, 'x');
    check(service.accept(1, 2, messages) == Result::InvalidMessage, "oversize message accepted");
    check(service.accept(1, 2, {}) == Result::InvalidMessage, "empty batch accepted");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
