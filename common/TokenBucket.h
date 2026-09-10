#pragma once

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace chat::runtime {
// Access from one executor only. Explicit time makes overload boundaries testable.
class TokenBucket {
  public:
    using Clock = std::chrono::steady_clock;
    TokenBucket(double capacity, double per_second, Clock::time_point now = Clock::now())
        : capacity_(capacity), rate_(per_second), tokens_(capacity), last_(now) {
        if (!(capacity > 0) || !(per_second > 0))
            throw std::invalid_argument("invalid token bucket limits");
    }
    bool consume(double amount, Clock::time_point now = Clock::now()) {
        if (now > last_) {
            tokens_ = std::min(
                capacity_, tokens_ + std::chrono::duration<double>(now - last_).count() * rate_);
            last_ = now;
        }
        if (!(amount >= 0) || amount > tokens_)
            return false;
        tokens_ -= amount;
        return true;
    }

  private:
    double capacity_, rate_, tokens_;
    Clock::time_point last_;
};
} // namespace chat::runtime
