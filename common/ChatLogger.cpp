#include "ChatLogger.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>

namespace chat::observability {
namespace {

std::mutex logger_mutex;
std::shared_ptr<logsystem::Logger> logger;
std::atomic<std::size_t> configured_queue_capacity{8192};
std::atomic<Level> configured_level{Level::Info};
std::atomic<std::int64_t> next_overflow_notice_ms{0};

std::string sanitized(std::string message) {
    // Redis ticket keys contain bearer credentials.  Legacy diagnostics used
    // to print complete keys, so redact their suffix before JSON serialization.
    constexpr const char* prefix = "chat_ticket_";
    std::size_t position = 0;
    while ((position = message.find(prefix, position)) != std::string::npos) {
        const auto begin = position + std::char_traits<char>::length(prefix);
        auto end = message.find_first_of(" \t\r\n]})", begin);
        if (end == std::string::npos)
            end = message.size();
        message.replace(begin, end - begin, "[redacted]");
        position = begin + 10;
    }
    return message;
}

std::string env(const char* name, const std::string& fallback) {
    const auto* value = std::getenv(name);
    return value && *value ? value : fallback;
}

bool envBool(const char* name, bool fallback) {
    auto value = env(name, fallback ? "true" : "false");
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "true" || value == "1" || value == "yes")
        return true;
    if (value == "false" || value == "0" || value == "no")
        return false;
    throw std::invalid_argument(std::string(name) + " must be true or false");
}

std::size_t envSize(const char* name, std::size_t fallback) {
    const auto text = env(name, std::to_string(fallback));
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed);
    if (consumed != text.size() || value == 0) {
        throw std::invalid_argument(std::string(name) + " must be a positive integer");
    }
    return static_cast<std::size_t>(value);
}

} // namespace

void initialize(const std::string& service_name) {
    std::lock_guard<std::mutex> lock(logger_mutex);
    if (logger)
        return;
    logsystem::LoggerOptions options;
    options.service_name = service_name;
    options.minimum_level = logsystem::parseLevel(env("CHAT_LOG_LEVEL", "info"));
    configured_level.store(options.minimum_level);
    options.console_output = envBool("CHAT_LOG_CONSOLE", true);
    options.file_output = envBool("CHAT_LOG_FILE_ENABLED", true);
    options.max_file_size_bytes = envSize("CHAT_LOG_MAX_FILE_MB", 50) * 1024U * 1024U;
    options.max_files = envSize("CHAT_LOG_MAX_FILES", 5);
    options.queue_capacity = envSize("CHAT_LOG_QUEUE_CAPACITY", 8192);
    configured_queue_capacity.store(options.queue_capacity);
    options.file_path =
        std::filesystem::path(env("CHAT_LOG_DIR", "./logs")) / (service_name + ".jsonl");
    logger = std::make_shared<logsystem::Logger>(std::move(options));
}

bool enabled(Level level) {
    return static_cast<int>(level) >= static_cast<int>(configured_level.load());
}

void log(Level level, const std::string& event, const std::string& message,
         std::initializer_list<Field> fields) {
    if (!enabled(level)) return;
    std::shared_ptr<logsystem::Logger> current;
    {
        std::lock_guard<std::mutex> lock(logger_mutex);
        current = logger;
    }
    if (!current)
        return;
    const auto before = current->droppedCount();
    current->write({level, event, message, {fields.begin(), fields.end()}});
    const auto after = current->droppedCount();
    if (after > before) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto next = next_overflow_notice_ms.load();
        if (now < next || !next_overflow_notice_ms.compare_exchange_strong(next, now + 10000))
            return;
        current->write({Level::Warn,
                        "logger.queue_overflow",
                        "bounded log queue was saturated",
                        {{"queue_depth", std::uint64_t(configured_queue_capacity.load())},
                         {"dropped_count", std::uint64_t(after)}}});
    }
}

void flush() {
    std::shared_ptr<logsystem::Logger> current;
    {
        std::lock_guard<std::mutex> lock(logger_mutex);
        current = logger;
    }
    if (current)
        current->flush();
}

void shutdown() {
    std::shared_ptr<logsystem::Logger> owned;
    {
        std::lock_guard<std::mutex> lock(logger_mutex);
        owned = std::move(logger);
    }
    if (owned)
        owned->shutdown();
}

std::uint64_t droppedCount() {
    std::shared_ptr<logsystem::Logger> current;
    {
        std::lock_guard<std::mutex> lock(logger_mutex);
        current = logger;
    }
    return current ? current->droppedCount() : 0;
}

std::string newRequestId() {
    thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<std::uint64_t> distribution;
    const auto high = distribution(generator);
    const auto low = distribution(generator);
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(8) << (high >> 32) << '-' << std::setw(4)
           << ((high >> 16) & 0xffff) << '-' << std::setw(4) << (high & 0xffff) << '-'
           << std::setw(4) << (low >> 48) << '-' << std::setw(12) << (low & 0xffffffffffffULL);
    return output.str();
}

LogStream::LogStream(Level level, std::string event)
    : level_(level), event_(std::move(event)), active_(enabled(level)) {}

LogStream::LogStream(LogStream&& other) noexcept
    : level_(other.level_), event_(std::move(other.event_)), active_(other.active_) {
    buffer_ << other.buffer_.str();
    other.active_ = false;
}

LogStream::~LogStream() {
    if (!active_)
        return;
    auto message = sanitized(buffer_.str());
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();
    if (!message.empty())
        log(level_, event_, message);
}

LogStream& LogStream::operator<<(std::ostream& (*manipulator)(std::ostream&)) {
    if (active_) manipulator(buffer_);
    return *this;
}

LogStream stream(Level level, std::string event) {
    return LogStream(level, std::move(event));
}

} // namespace chat::observability
