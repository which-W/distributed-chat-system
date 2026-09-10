#include "TokenBucket.h"
#include <iostream>
#include <stdexcept>

void check(bool value) {
    if (!value)
        throw std::runtime_error("token budget invariant failed");
}
int main() try {
    using namespace std::chrono_literals;
    using chat::runtime::TokenBucket;
    const TokenBucket::Clock::time_point start{};
    TokenBucket chat(200, 100, start), file(256, 128, start);
    check(chat.consume(200, start));
    check(!chat.consume(1, start));
    check(file.consume(256, start)); // Classes have independent budgets.
    check(chat.consume(50, start + 500ms));
    check(!chat.consume(1, start + 500ms));
    check(!chat.consume(1, start)); // Clock regression cannot refill.
    check(chat.consume(200, start + 10s));
    check(!chat.consume(1, start + 10s)); // Idle time never exceeds burst capacity.
    TokenBucket bytes(1024, 512, start);
    check(!bytes.consume(1025, start));
    check(!bytes.consume(-1, start));
    check(bytes.consume(1024, start));
    check(bytes.consume(512, start + 1s));
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
