#include <LogQueue.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
template <typename F> double measure(F&& fn) {
    const auto begin = Clock::now();
    fn();
    return std::chrono::duration<double>(Clock::now() - begin).count();
}
std::string serialize(std::size_t index) {
    // Identical serialization on each producer path. No timestamp, rotation or
    // console output: this isolates the queue/sink tradeoff, not full Logger cost.
    std::ostringstream output;
    output << "{\"event\":\"benchmark\",\"message\":\"equal payload\",\"sequence\":" << index
           << '}';
    return output.str();
}
} // namespace
int main(int argc, char** argv) try {
    const auto count = argc > 1 ? std::stoull(argv[1]) : 200000;
    if (!count)
        throw std::runtime_error("count must be positive");
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("chat-log-bench-" + std::to_string(Clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    std::ofstream sync(directory / "sync.jsonl", std::ios::binary);
    std::ofstream async(directory / "async.jsonl", std::ios::binary);
    if (!sync || !async)
        throw std::runtime_error("cannot open benchmark output");
    const auto sync_seconds = measure([&] {
        for (std::size_t i = 0; i < count; ++i)
            sync << serialize(i) << '\n';
        sync.flush(); // Same flush policy as asynchronous path; neither is fsync.
    });
    logsystem::LogQueue queue(8192);
    std::atomic<std::uint64_t> dropped{0}, written{0};
    std::thread consumer([&] {
        std::string line;
        while (queue.pop(line)) {
            async << line << '\n';
            ++written;
        }
        async.flush();
    });
    const auto producer_seconds = measure([&] {
        for (std::size_t i = 0; i < count; ++i)
            if (!queue.push(serialize(i), std::chrono::milliseconds(1000)))
                ++dropped;
    });
    const auto total_seconds = producer_seconds + measure([&] {
                                   queue.shutdown();
                                   consumer.join();
                               });
    if (!sync || !async)
        throw std::runtime_error("benchmark output failed");
    sync.close();
    async.close();
    bool equal = true;
    std::ifstream left(directory / "sync.jsonl"), right(directory / "async.jsonl");
    std::string a, b;
    std::uint64_t lines = 0;
    while (std::getline(left, a)) {
        if (!std::getline(right, b) || a != b) {
            equal = false;
            break;
        }
        ++lines;
    }
    if (std::getline(right, b) || lines != count)
        equal = false;
    std::cout << "{\"schema_version\":2,\"mode\":\"equivalent_serialization_queue_sink\",\"count\":"
              << count << ",\"sync_seconds\":" << sync_seconds
              << ",\"async_producer_seconds\":" << producer_seconds
              << ",\"async_total_seconds\":" << total_seconds << ",\"dropped\":" << dropped.load()
              << ",\"written\":" << written.load()
              << ",\"identical_output\":" << (equal ? "true" : "false") << "}\n";
    return dropped || !equal ? 1 : 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
