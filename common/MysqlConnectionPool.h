#pragma once

#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/exception.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/mysql_driver.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace chat::storage {

// 归还池化连接时统一撤销未完成事务，防止锁和未提交数据泄漏到下一个请求。
template <typename Connection>
void ResetMysqlTransaction(Connection& connection)
{
    if (!connection.getAutoCommit()) {
        connection.rollback();
        connection.setAutoCommit(true);
    }
}

class SqlConnection {
public:
    SqlConnection(sql::Connection* connection, std::int64_t last_operation_time)
        : _con(connection), _last_oper_time(last_operation_time)
    {
    }

    std::unique_ptr<sql::Connection> _con;
    std::int64_t _last_oper_time;
};

class MySqlPool {
public:
    using ConnectionFactory = std::function<std::unique_ptr<SqlConnection>()>;
    using ConnectionAction = std::function<void(SqlConnection&)>;

    struct Options {
        std::size_t pool_size = 1;
        std::chrono::milliseconds borrow_timeout = std::chrono::seconds(5);
        std::chrono::milliseconds health_interval = std::chrono::seconds(60);
        bool start_health_worker = true;
    };

    MySqlPool(const std::string& url, const std::string& user, const std::string& pass,
        const std::string& schema, int pool_size)
        : MySqlPool(
            Options {static_cast<std::size_t>(pool_size), std::chrono::seconds(5),
                std::chrono::seconds(60), true},
            [url, user, pass, schema]() {
                auto* driver = sql::mysql::get_mysql_driver_instance();
                std::unique_ptr<sql::Connection> connection(driver->connect(url, user, pass));
                connection->setSchema(schema);
                const auto now = std::chrono::system_clock::now().time_since_epoch();
                const auto timestamp =
                    std::chrono::duration_cast<std::chrono::seconds>(now).count();
                return std::make_unique<SqlConnection>(connection.release(), timestamp);
            },
            [](SqlConnection& connection) {
                if (!connection._con) {
                    throw std::runtime_error("MySQL connection is null");
                }
                ResetMysqlTransaction(*connection._con);
            },
            [](SqlConnection& connection) {
                if (!connection._con) {
                    throw std::runtime_error("MySQL connection is null");
                }
                const auto now = std::chrono::system_clock::now().time_since_epoch();
                const auto timestamp =
                    std::chrono::duration_cast<std::chrono::seconds>(now).count();
                if (timestamp - connection._last_oper_time < 5) {
                    return;
                }
                std::unique_ptr<sql::Statement> statement(connection._con->createStatement());
                std::unique_ptr<sql::ResultSet> result(statement->executeQuery("SELECT 1"));
                connection._last_oper_time = timestamp;
            })
    {
    }

    // 该构造函数仅用于内存假后端测试，不改变生产 DAO 的公开接口。
    MySqlPool(Options options, ConnectionFactory factory, ConnectionAction sanitizer,
        ConnectionAction health_check)
        : options_(std::move(options)), factory_(std::move(factory)),
          sanitizer_(std::move(sanitizer)), health_check_(std::move(health_check))
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

    MySqlPool(const MySqlPool&) = delete;
    MySqlPool& operator=(const MySqlPool&) = delete;

    ~MySqlPool()
    {
        Close();
    }

    std::unique_ptr<SqlConnection> getConnection()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool available = connection_ready_.wait_for(lock, options_.borrow_timeout, [this]() {
            return stopped_ || !connections_.empty();
        });
        if (!available || stopped_) {
            return nullptr;
        }
        auto connection = std::move(connections_.front());
        connections_.pop();
        return connection;
    }

    void returnConnection(std::unique_ptr<SqlConnection> connection)
    {
        if (!connection) {
            return;
        }

        try {
            sanitizer_(*connection);
        }
        catch (const std::exception& error) {
            std::cerr << "MySQL connection sanitation failed: " << error.what() << std::endl;
            DiscardConnection(std::move(connection));
            // 先同步补建一次；失败后健康线程仍会按缺口继续重试。
            CreateOneConnection();
            health_wakeup_.notify_all();
            return;
        }
        catch (...) {
            std::cerr << "MySQL connection sanitation failed" << std::endl;
            DiscardConnection(std::move(connection));
            CreateOneConnection();
            health_wakeup_.notify_all();
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            if (live_connections_ > 0) {
                --live_connections_;
            }
            return;
        }
        connections_.push(std::move(connection));
        connection_ready_.notify_one();
    }

    void Close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) {
                // 即使其他线程先执行了 Close，也仍需由析构线程完成 join。
            }
            else {
                stopped_ = true;
            }
        }
        connection_ready_.notify_all();
        health_wakeup_.notify_all();
        if (health_thread_.joinable()) {
            health_thread_.join();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        while (!connections_.empty()) {
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
    bool CreateOneConnection()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_ || live_connections_ >= options_.pool_size) {
                return false;
            }
            // 先预留名额，防止归还线程与健康线程并发补建造成超额连接。
            ++live_connections_;
        }

        std::unique_ptr<SqlConnection> connection;
        try {
            connection = factory_();
        }
        catch (const std::exception& error) {
            std::cerr << "MySQL connection creation failed: " << error.what() << std::endl;
        }
        catch (...) {
            std::cerr << "MySQL connection creation failed" << std::endl;
        }

        if (!connection) {
            std::lock_guard<std::mutex> lock(mutex_);
            --live_connections_;
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            --live_connections_;
            return false;
        }
        connections_.push(std::move(connection));
        connection_ready_.notify_one();
        return true;
    }

    void DiscardConnection(std::unique_ptr<SqlConnection> connection)
    {
        connection.reset();
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
            std::unique_ptr<SqlConnection> connection;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopped_ || connections_.empty()) {
                    break;
                }
                connection = std::move(connections_.front());
                connections_.pop();
            }

            try {
                health_check_(*connection);
            }
            catch (...) {
                DiscardConnection(std::move(connection));
                continue;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) {
                if (live_connections_ > 0) {
                    --live_connections_;
                }
            }
            else {
                connections_.push(std::move(connection));
                connection_ready_.notify_one();
            }
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
    ConnectionFactory factory_;
    ConnectionAction sanitizer_;
    ConnectionAction health_check_;
    mutable std::mutex mutex_;
    std::condition_variable connection_ready_;
    std::condition_variable health_wakeup_;
    std::queue<std::unique_ptr<SqlConnection>> connections_;
    std::size_t live_connections_ = 0;
    bool stopped_ = false;
    std::thread health_thread_;
};

} // namespace chat::storage
