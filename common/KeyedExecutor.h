#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace chat::runtime {

// A key always maps to the same worker, including before/after authentication.
// Blocking dependencies occupy one shard, never an unbounded number of threads.
class KeyedExecutor {
  public:
    using Clock = std::chrono::steady_clock;
    using Tick = std::function<void(std::size_t)>;
    struct Metrics {
        std::uint64_t queued, completed, rejected, failures, wait_us, run_us, cancelled;
    };

    KeyedExecutor(std::size_t count, std::size_t capacity, Tick tick = {},
                  std::chrono::milliseconds interval = std::chrono::seconds(1))
        : capacity_(capacity), tick_(std::move(tick)), interval_(interval) {
        if (!count || !capacity || interval.count() <= 0)
            throw std::invalid_argument("invalid executor capacity/interval");
        for (std::size_t i = 0; i < count; ++i)
            shards_.push_back(std::make_unique<Shard>());
        try {
            for (std::size_t i = 0; i < count; ++i)
                shards_[i]->thread = std::thread([this, i] { run(i); });
        } catch (...) {
            stop();
            throw;
        }
    }
    ~KeyedExecutor() {
        stop();
    }
    KeyedExecutor(const KeyedExecutor&) = delete;
    KeyedExecutor& operator=(const KeyedExecutor&) = delete;

    std::size_t shard(std::size_t key) const {
        return key % shards_.size();
    }
    bool post(std::size_t key, std::function<void()> task) {
        auto& s = *shards_[shard(key)];
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            if (s.stopping || s.queue.size() >= capacity_) {
                ++rejected_;
                return false;
            }
            s.queue.push({Clock::now(), std::move(task)});
            ++queued_;
        }
        s.ready.notify_one();
        return true;
    }
    // Caller must not be a worker. Cancellation leaves running tasks to finish.
    void stop(bool drain = true) {
        std::lock_guard<std::mutex> stopping(stop_mutex_);
        for (auto& s : shards_) {
            std::queue<Task> discarded;
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                s->stopping = true;
                if (!drain) {
                    cancelled_ += s->queue.size();
                    queued_ -= s->queue.size();
                    discarded.swap(s->queue);
                }
            }
            s->ready.notify_one();
        }
        for (auto& s : shards_)
            if (s->thread.joinable())
                s->thread.join();
    }
    Metrics metrics() const {
        return {queued_.load(),  completed_.load(), rejected_.load(), failures_.load(),
                wait_us_.load(), run_us_.load(),    cancelled_.load()};
    }

  private:
    struct Task {
        Clock::time_point enqueued;
        std::function<void()> call;
    };
    struct Shard {
        std::mutex mutex;
        std::condition_variable ready;
        std::queue<Task> queue;
        std::thread thread;
        bool stopping = false;
    };
    void invoke(const std::function<void()>& fn) {
        try {
            fn();
        } catch (...) {
            ++failures_;
        }
    }
    void run(std::size_t index) {
        auto& s = *shards_[index];
        auto next = Clock::now() + interval_;
        for (;;) {
            std::unique_lock<std::mutex> lock(s.mutex);
            s.ready.wait_until(lock, next, [&] { return s.stopping || !s.queue.empty(); });
            if (s.stopping && s.queue.empty())
                return;
            // Timers cannot be starved by a permanently nonempty queue.
            if (!s.stopping && Clock::now() >= next) {
                lock.unlock();
                if (tick_)
                    invoke([&] { tick_(index); });
                next = Clock::now() + interval_;
                continue;
            }
            if (s.queue.empty())
                continue;
            auto task = std::move(s.queue.front());
            s.queue.pop();
            --queued_;
            lock.unlock();
            auto start = Clock::now();
            wait_us_ += std::chrono::duration_cast<std::chrono::microseconds>(start - task.enqueued)
                            .count();
            invoke(task.call);
            run_us_ +=
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
            ++completed_;
        }
    }
    std::size_t capacity_;
    Tick tick_;
    std::chrono::milliseconds interval_;
    std::vector<std::unique_ptr<Shard>> shards_;
    std::mutex stop_mutex_;
    std::atomic<std::uint64_t> cancelled_{0};
    std::atomic<std::uint64_t> queued_{0}, completed_{0}, rejected_{0}, failures_{0}, wait_us_{0},
        run_us_{0};
};
} // namespace chat::runtime
