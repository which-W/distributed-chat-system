#include "MysqlConnectionPool.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using chat::storage::MySqlPool;
using chat::storage::SqlConnection;

bool Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

struct FakeNativeConnection {
    bool auto_commit = false;
    std::vector<std::string> calls;

    bool getAutoCommit()
    {
        calls.push_back("getAutoCommit");
        return auto_commit;
    }
    void rollback() { calls.push_back("rollback"); }
    void setAutoCommit(bool value)
    {
        calls.push_back("setAutoCommit");
        auto_commit = value;
    }
};

MySqlPool::Options TestOptions(std::size_t size, bool worker = false)
{
    return {size, std::chrono::milliseconds(80), std::chrono::milliseconds(10), worker};
}

} // namespace

int main()
{
    FakeNativeConnection native;
    chat::storage::ResetMysqlTransaction(native);
    if (!Require(native.auto_commit, "事务清理后未恢复 autocommit")
        || !Require(native.calls == std::vector<std::string> {
                "getAutoCommit", "rollback", "setAutoCommit"},
            "事务清理调用顺序错误")) {
        return 1;
    }

    {
        int next_id = 0;
        MySqlPool pool(TestOptions(1),
            [&]() { return std::make_unique<SqlConnection>(nullptr, ++next_id); },
            [](SqlConnection&) {}, [](SqlConnection&) {});
        auto first = pool.getConnection();
        if (!Require(first && first->_last_oper_time == 1, "未借到初始假连接")) return 1;
        pool.returnConnection(std::move(first));
        auto reused = pool.getConnection();
        if (!Require(reused && reused->_last_oper_time == 1, "健康连接没有重新入池")) return 1;
        pool.returnConnection(std::move(reused));
    }

    {
        int next_id = 0;
        MySqlPool pool(TestOptions(1),
            [&]() { return std::make_unique<SqlConnection>(nullptr, ++next_id); },
            [](SqlConnection& connection) {
                if (connection._last_oper_time == 1) throw std::runtime_error("dirty");
            }, [](SqlConnection&) {});
        auto dirty = pool.getConnection();
        pool.returnConnection(std::move(dirty));
        auto replacement = pool.getConnection();
        if (!Require(replacement && replacement->_last_oper_time == 2,
                "清理失败后未使用替换连接")
            || !Require(pool.liveConnectionCountForTest() == 1, "替换后连接容量错误")) {
            return 1;
        }
        pool.returnConnection(std::move(replacement));
    }

    {
        std::atomic<int> factory_calls = 0;
        MySqlPool pool(TestOptions(1, true),
            [&]() -> std::unique_ptr<SqlConnection> {
                const int call = ++factory_calls;
                if (call == 2) return nullptr; // 同步补建失败，后台线程必须继续恢复。
                return std::make_unique<SqlConnection>(nullptr, call);
            },
            [](SqlConnection& connection) {
                if (connection._last_oper_time == 1) throw std::runtime_error("dirty");
            }, [](SqlConnection&) {});
        auto dirty = pool.getConnection();
        pool.returnConnection(std::move(dirty));
        auto recovered = pool.getConnection();
        if (!Require(recovered && recovered->_last_oper_time >= 3,
                "首次替换失败后后台线程未恢复容量")) {
            return 1;
        }
        pool.returnConnection(std::move(recovered));
    }

    {
        auto options = TestOptions(1);
        options.borrow_timeout = std::chrono::milliseconds(25);
        MySqlPool empty(options, []() { return std::unique_ptr<SqlConnection>(); },
            [](SqlConnection&) {}, [](SqlConnection&) {});
        const auto started = std::chrono::steady_clock::now();
        if (!Require(!empty.getConnection(), "空池不应返回连接")) return 1;
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (!Require(elapsed < std::chrono::milliseconds(250), "空池等待没有按时结束")) return 1;
    }

    {
        auto options = TestOptions(0);
        options.borrow_timeout = std::chrono::seconds(2);
        MySqlPool pool(options, []() { return std::unique_ptr<SqlConnection>(); },
            [](SqlConnection&) {}, [](SqlConnection&) {});
        auto waiter = std::async(std::launch::async, [&pool]() { return pool.getConnection(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pool.Close();
        if (!Require(waiter.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready
                && !waiter.get(),
            "Close 未及时唤醒 MySQL 等待者")) {
            return 1;
        }
    }

    {
        auto options = TestOptions(0, true);
        options.health_interval = std::chrono::hours(1);
        const auto started = std::chrono::steady_clock::now();
        { MySqlPool pool(options, []() { return std::unique_ptr<SqlConnection>(); },
              [](SqlConnection&) {}, [](SqlConnection&) {}); }
        if (!Require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(250),
                "MySQL 健康线程析构未被唤醒并 join")) {
            return 1;
        }
    }
    return 0;
}
