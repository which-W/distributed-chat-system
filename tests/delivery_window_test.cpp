#include "DeliveryWindow.h"
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() try {
    using namespace chat::messages;
    using namespace std::chrono_literals;
    DeliveryWindow window;
    const auto now = DeliveryWindow::Clock::now();
    TextMessage a{"same-id", 1, 9, "a"}, b{"same-id", 2, 9, "b"};
    check(window.due(a, now), "new message must be ready");
    check(!window.sent(a, now), "first attempt is not a retry");
    check(!window.due(a, now + 4s), "retry must wait");
    check(window.due(a, now + 5s), "lost ACK must be retried online");
    check(window.due(b, now), "deduplication must include sender");
    check(window.sent(a, now + 5s), "second attempt must count as retry");
    check(!window.due(a, now + 14s), "retry must back off");
    check(window.due(a, now + 15s), "backoff must eventually expire");
    window.reconcile({a});
    check(!window.due(a, now + 6s), "poll must preserve retry deadline");
    window.reconcile({b});
    check(window.size() == 0, "committed ACK must retire old entry");

    // Simulate 1,000 durable messages and ACK-driven refill without reconnect.
    std::vector<TextMessage> backlog;
    for (int i = 0; i < 1000; ++i)
        backlog.push_back({std::to_string(i), 1, 9, "payload"});
    std::size_t delivered = 0;
    while (!backlog.empty()) {
        const auto count = std::min(backlog.size(), std::size_t(DeliveryWindow::Capacity));
        std::vector<TextMessage> page(backlog.begin(), backlog.begin() + count);
        window.reconcile(page);
        for (const auto& message : page) {
            check(window.due(message, now), "new page must be immediately ready");
            window.sent(message, now);
            ++delivered;
        }
        check(window.size() <= DeliveryWindow::Capacity, "window must remain bounded");
        backlog.erase(backlog.begin(), backlog.begin() + count);
    }
    window.reconcile({});
    check(delivered == 1000 && window.size() == 0, "all messages must drain");
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
