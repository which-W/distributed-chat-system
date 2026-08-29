#pragma once

#include <hiredis/hiredis.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace chat::storage {

struct RedisApi {
    std::function<redisContext*(const std::string&, int)> connect;
    std::function<bool(redisContext*)> context_ok;
    std::function<redisReply*(redisContext*, const std::vector<std::string>&)> command;
    std::function<void(redisReply*)> free_reply;
    std::function<void(redisContext*)> free_context;
};

inline RedisApi ProductionRedisApi()
{
    return RedisApi {
        [](const std::string& host, int port) { return redisConnect(host.c_str(), port); },
        [](redisContext* context) { return context != nullptr && context->err == 0; },
        [](redisContext* context, const std::vector<std::string>& arguments) {
            std::vector<const char*> argv;
            std::vector<std::size_t> lengths;
            argv.reserve(arguments.size());
            lengths.reserve(arguments.size());
            for (const auto& argument : arguments) {
                argv.push_back(argument.data());
                lengths.push_back(argument.size());
            }
            return static_cast<redisReply*>(redisCommandArgv(
                context, static_cast<int>(argv.size()), argv.data(), lengths.data()));
        },
        [](redisReply* reply) {
            if (reply) {
                freeReplyObject(reply);
            }
        },
        [](redisContext* context) {
            if (context) {
                redisFree(context);
            }
        }};
}

class RedisConnectionPool {
public:
    struct Options {
        std::size_t pool_size = 1;
        std::chrono::milliseconds borrow_timeout = std::chrono::seconds(5);
        std::chrono::milliseconds health_interval = std::chrono::seconds(60);
        bool start_health_worker = true;
    };

    RedisConnectionPool(std::size_t pool_size, const std::string& host, int port,
        const std::string& password)
        : RedisConnectionPool(pool_size, host, port, "", password)
    {
    }

    RedisConnectionPool(std::size_t pool_size, const std::string& host, int port,
        const std::string& user, const std::string& password)
        : RedisConnectionPool(
            Options {pool_size, std::chrono::seconds(5), std::chrono::seconds(60), true},
            host, port, user, password, ProductionRedisApi())
    {
    }

    // 假 hiredis API 可稳定制造空回复、认证错误和断线，不依赖外部 Redis。
    RedisConnectionPool(Options options, std::string host, int port, std::string user,
        std::string password, RedisApi api)
        : options_(std::move(options)), host_(std::move(host)), port_(port),
          user_(std::move(user)), password_(std::move(password)), api_(std::move(api))
    {
        for (std::size_t index = 0; index < options_.pool_size; ++index) {
            if (!CreateOneConnection()) {
                break;
            }
        }
        if (options_.start_health_worker) {
            health_thread_ = std::thread([this]() { HealthWorker(); });
        }
    }

    RedisConnectionPool(const RedisConnectionPool&) = delete;
    RedisConnectionPool& operator=(const RedisConnectionPool&) = delete;

    ~RedisConnectionPool()
    {
        Close();
    }

    redisContext* getConnection()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool available = connection_ready_.wait_for(lock, options_.borrow_timeout, [this]() {
            return stopped_ || !connections_.empty();
        });
        if (!available || stopped_) {
            return nullptr;
        }
        auto* context = connections_.front();
        connections_.pop();
        return context;
    }

    redisContext* getContext()
    {
        return getConnection();
    }

    redisContext* getConNonBlock()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || connections_.empty()) {
            return nullptr;
        }
        auto* context = connections_.front();
        connections_.pop();
        return context;
    }

    void returnConnection(redisContext* context)
    {
        if (!context) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            if (live_connections_ > 0) {
                --live_connections_;
            }
            api_.free_context(context);
            return;
        }
        connections_.push(context);
        connection_ready_.notify_one();
    }

    void returnContext(redisContext* context)
    {
        returnConnection(context);
    }

    void Close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        connection_ready_.notify_all();
        health_wakeup_.notify_all();
        if (health_thread_.joinable()) {
            health_thread_.join();
        }
        ClearConnections();
    }

    void ClearConnections()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!connections_.empty()) {
            api_.free_context(connections_.front());
            connections_.pop();
            if (live_connections_ > 0) {
                --live_connections_;
            }
        }
    }

    std::size_t liveConnectionCountForTest() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return live_connections_;
    }

private:
    bool Authenticate(redisContext* context)
    {
        if (password_.empty()) {
            return true;
        }
        std::vector<std::string> arguments {"AUTH"};
        if (!user_.empty()) {
            arguments.push_back(user_);
        }
        arguments.push_back(password_);
        auto* reply = api_.command(context, arguments);
        if (!reply) {
            return false;
        }
        const bool authenticated = reply->type == REDIS_REPLY_STATUS && reply->str != nullptr
            && reply->len == 2
            && ((reply->str[0] == 'O' && reply->str[1] == 'K')
                || (reply->str[0] == 'o' && reply->str[1] == 'k'));
        api_.free_reply(reply);
        return authenticated;
    }

    bool CreateOneConnection()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_ || live_connections_ >= options_.pool_size) {
                return false;
            }
            ++live_connections_;
        }

        auto* context = api_.connect(host_, port_);
        if (!context || !api_.context_ok(context) || !Authenticate(context)
            || !api_.context_ok(context)) {
            if (context) {
                api_.free_context(context);
            }
            std::lock_guard<std::mutex> lock(mutex_);
            --live_connections_;
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            --live_connections_;
            api_.free_context(context);
            return false;
        }
        connections_.push(context);
        connection_ready_.notify_one();
        return true;
    }

    void DiscardConnection(redisContext* context)
    {
        api_.free_context(context);
        std::lock_guard<std::mutex> lock(mutex_);
        if (live_connections_ > 0) {
            --live_connections_;
        }
    }

    void CheckIdleConnections()
    {
        std::size_t idle_count = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            idle_count = connections_.size();
        }
        for (std::size_t index = 0; index < idle_count; ++index) {
            auto* context = getConNonBlock();
            if (!context) {
                break;
            }
            auto* reply = api_.command(context, {"PING"});
            const bool healthy = reply != nullptr && reply->type == REDIS_REPLY_STATUS
                && reply->str != nullptr && reply->len == 4
                && std::memcmp(reply->str, "PONG", 4) == 0;
            if (reply) {
                api_.free_reply(reply);
            }
            if (!healthy || !api_.context_ok(context)) {
                DiscardConnection(context);
                continue;
            }
            returnConnection(context);
        }
    }

    void ReplenishConnections()
    {
        while (CreateOneConnection()) {
        }
    }

    void HealthWorker()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopped_) {
            health_wakeup_.wait_for(lock, options_.health_interval);
            if (stopped_) {
                break;
            }
            lock.unlock();
            CheckIdleConnections();
            ReplenishConnections();
            lock.lock();
        }
    }

    Options options_;
    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    RedisApi api_;
    mutable std::mutex mutex_;
    std::condition_variable connection_ready_;
    std::condition_variable health_wakeup_;
    std::queue<redisContext*> connections_;
    std::size_t live_connections_ = 0;
    bool stopped_ = false;
    std::thread health_thread_;
};

} // namespace chat::storage
