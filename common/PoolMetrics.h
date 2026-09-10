#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace chat::observability {
struct PoolMetrics {
    std::atomic<std::uint64_t> borrows{0}, failed_borrows{0}, wait_us{0};
};
inline PoolMetrics mysql_pool_metrics, redis_pool_metrics;
class BorrowTimer {
  public:
    explicit BorrowTimer(PoolMetrics& metrics) : metrics_(metrics) {}
    ~BorrowTimer() {
        ++metrics_.borrows;
        if (!success_)
            ++metrics_.failed_borrows;
        metrics_.wait_us += std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - start_)
                                .count();
    }
    void success() {
        success_ = true;
    }

  private:
    PoolMetrics& metrics_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    bool success_ = false;
};
} // namespace chat::observability
