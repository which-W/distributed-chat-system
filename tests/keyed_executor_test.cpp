#include "KeyedExecutor.h"
#include <future>
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() try {
    using namespace std::chrono_literals;
    std::atomic<int> ticks{0};
    chat::runtime::KeyedExecutor executor(
        2, 2, [&](std::size_t) { ++ticks; }, 5ms);
    std::promise<void> entered, release, independent;
    auto barrier = release.get_future().share();
    std::vector<int> order;
    executor.post(0, [&] {
        entered.set_value();
        barrier.wait();
        order.push_back(0);
    });
    if (entered.get_future().wait_for(2s) != std::future_status::ready) {
        release.set_value();
        throw std::runtime_error("worker did not start");
    }
    bool first = executor.post(0, [&] { order.push_back(1); });
    bool second = executor.post(0, [&] { order.push_back(2); });
    bool overflow = executor.post(0, [] {});
    executor.post(1, [&] { independent.set_value(); });
    auto independent_status = independent.get_future().wait_for(2s);
    release.set_value();
    executor.stop();
    check(first && second && !overflow, "bounded queue must reject overflow");
    check(independent_status == std::future_status::ready,
          "blocked shard must not block other shard");
    check(order == std::vector<int>({0, 1, 2}), "accepted tasks must drain in key order");
    check(!executor.post(1, [] {}), "stopped executor must reject work");
    check(executor.metrics().queued == 0 && executor.metrics().completed == 4,
          "drain counters must match accepted work");

    std::promise<void> ticked, after_exception;
    std::atomic<bool> notified{false};
    chat::runtime::KeyedExecutor resilient(
        1, 8,
        [&](std::size_t) {
            if (!notified.exchange(true))
                ticked.set_value();
        },
        5ms);
    resilient.post(0, [] { throw std::runtime_error("injected task error"); });
    resilient.post(0, [&] { after_exception.set_value(); });
    auto progress = after_exception.get_future().wait_for(2s);
    auto timer = ticked.get_future().wait_for(2s);
    resilient.stop();
    check(progress == std::future_status::ready && timer == std::future_status::ready,
          "exception must not kill worker or maintenance timer");
    check(resilient.metrics().failures == 1, "exception counter must increment");
    chat::runtime::KeyedExecutor cancellable(1, 8);
    std::promise<void> running, finish;
    auto finish_signal = finish.get_future().share();
    std::atomic<int> unexpected{0};
    cancellable.post(0, [&] {
        running.set_value();
        finish_signal.wait();
    });
    running.get_future().wait();
    for (int i = 0; i < 8; ++i)
        cancellable.post(0, [&] { ++unexpected; });
    auto stopped = std::async(std::launch::async, [&] { cancellable.stop(false); });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (cancellable.metrics().cancelled != 8 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const auto cancelled = cancellable.metrics().cancelled;
    finish.set_value();
    stopped.get();
    check(cancelled == 8 && unexpected == 0, "cancellation must discard queued callbacks");
    check(cancellable.metrics().completed == 1 && cancellable.metrics().queued == 0,
          "cancellation must finish running task and reconcile counters");
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
