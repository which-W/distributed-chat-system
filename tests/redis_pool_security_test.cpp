#include "RedisConnectionPool.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct FakeRedisState {
    enum class AuthMode { Success, NullReply, ErrorReply };
    std::mutex mutex;
    std::vector<std::vector<std::string>> commands;
    std::atomic<int> connected = 0;
    std::atomic<int> freed_contexts = 0;
    std::atomic<int> freed_replies = 0;
    std::atomic<int> ping_failures = 0;
    AuthMode auth_mode = AuthMode::Success;
};

redisReply* MakeReply(int type, const char* value)
{
    auto* reply = new redisReply {};
    reply->type = type;
    if (value) {
        const auto size = std::strlen(value);
        reply->str = static_cast<char*>(std::malloc(size + 1));
        std::memcpy(reply->str, value, size + 1);
        reply->len = size;
    }
    return reply;
}

chat::storage::RedisApi MakeApi(FakeRedisState& state)
{
    return {
        [&state](const std::string&, int) {
            ++state.connected;
            return reinterpret_cast<redisContext*>(new int(1));
        },
        [](redisContext* context) { return context != nullptr; },
        [&state](redisContext*, const std::vector<std::string>& arguments) -> redisReply* {
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.commands.push_back(arguments);
            }
            if (!arguments.empty() && arguments.front() == "AUTH") {
                if (state.auth_mode == FakeRedisState::AuthMode::NullReply) return nullptr;
                if (state.auth_mode == FakeRedisState::AuthMode::ErrorReply) {
                    return MakeReply(REDIS_REPLY_ERROR, "ERR invalid password");
                }
                return MakeReply(REDIS_REPLY_STATUS, "OK");
            }
            if (!arguments.empty() && arguments.front() == "PING"
                && state.ping_failures.fetch_sub(1) > 0) {
                return MakeReply(REDIS_REPLY_ERROR, "ERR disconnected");
            }
            return MakeReply(REDIS_REPLY_STATUS, "PONG");
        },
        [&state](redisReply* reply) {
            ++state.freed_replies;
            std::free(reply->str);
            delete reply;
        },
        [&state](redisContext* context) {
            ++state.freed_contexts;
            delete reinterpret_cast<int*>(context);
        }};
}

chat::storage::RedisConnectionPool::Options Options(bool worker = false)
{
    return {1, std::chrono::milliseconds(50), std::chrono::milliseconds(10), worker};
}

bool Require(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

std::vector<std::string> FirstCommand(FakeRedisState& state)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.commands.empty() ? std::vector<std::string>() : state.commands.front();
}

} // namespace

int main()
{
    {
        FakeRedisState state;
        chat::storage::RedisConnectionPool pool(Options(), "127.0.0.1", 6379, "", "", MakeApi(state));
        if (!Require(FirstCommand(state).empty(), "空密码不应发送 AUTH")) return 1;
    }
    {
        FakeRedisState state;
        chat::storage::RedisConnectionPool pool(
            Options(), "127.0.0.1", 6379, "", "secret", MakeApi(state));
        if (!Require(FirstCommand(state) == std::vector<std::string> {"AUTH", "secret"},
                "兼容认证参数错误")) return 1;
    }
    {
        FakeRedisState state;
        chat::storage::RedisConnectionPool pool(
            Options(), "127.0.0.1", 6379, "chat", "secret", MakeApi(state));
        if (!Require(FirstCommand(state)
                    == std::vector<std::string> {"AUTH", "chat", "secret"},
                "ACL 认证参数错误")) return 1;
    }
    for (const auto mode : {FakeRedisState::AuthMode::NullReply,
             FakeRedisState::AuthMode::ErrorReply}) {
        FakeRedisState state;
        state.auth_mode = mode;
        chat::storage::RedisConnectionPool pool(
            Options(), "127.0.0.1", 6379, "chat", "bad", MakeApi(state));
        if (!Require(pool.getConnection() == nullptr, "AUTH 失败连接不应进入池")
            || !Require(state.freed_contexts == 1, "AUTH 失败连接未释放")) {
            return 1;
        }
    }
    {
        FakeRedisState state;
        state.ping_failures = 1;
        chat::storage::RedisConnectionPool pool(
            Options(true), "127.0.0.1", 6379, "chat", "secret", MakeApi(state));
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        if (!Require(state.connected >= 2, "PING 失败后未重建 Redis 连接")) return 1;
        auto* recovered = pool.getConnection();
        if (!Require(recovered != nullptr, "Redis 重连后未恢复池容量")) return 1;
        pool.returnConnection(recovered);
    }
    {
        FakeRedisState state;
        auto options = Options();
        options.pool_size = 0;
        options.borrow_timeout = std::chrono::seconds(2);
        chat::storage::RedisConnectionPool pool(
            options, "127.0.0.1", 6379, "", "", MakeApi(state));
        auto waiter = std::async(std::launch::async, [&pool]() { return pool.getConnection(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pool.Close();
        if (!Require(waiter.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready
                && waiter.get() == nullptr,
            "Close 未唤醒 Redis 等待者")) return 1;
        pool.Close();
    }
    {
        FakeRedisState state;
        chat::storage::RedisConnectionPool pool(
            Options(), "127.0.0.1", 6379, "", "", MakeApi(state));
        auto* borrowed = pool.getConnection();
        pool.Close();
        pool.returnConnection(borrowed);
        if (!Require(state.freed_contexts == 1, "Close 后归还的 Redis 连接未释放")) return 1;
    }
    return 0;
}
