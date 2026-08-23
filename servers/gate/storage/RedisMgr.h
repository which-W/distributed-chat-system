#pragma once
#include"const.h"
#include"Singleton.h"
#include<hiredis/hiredis.h>
#include<queue>
#include<atomic>
#include"ConfigMgr.h"
class RedisConPool {
public:
    RedisConPool(size_t pool_size, const char* host, int port, const char* pwd );
    ~RedisConPool();
    redisContext* getContext();
    void returnContext(redisContext* context);
    void Close();


private:
    std::atomic<bool>_b_stop_;
    std::mutex _mutex;
    std::condition_variable _cond;
    std::queue<redisContext*> _redis_connections;
    const char* _host;
    size_t _pool_size;
    int _port;
};

class RedisMgr :public Singleton<RedisMgr>,std::enable_shared_from_this<RedisMgr> {
    friend class Singleton<RedisMgr>;
public:
    enum class VerificationResult {
        Success,
        Expired,
        Mismatch,
        TooManyAttempts,
        RedisError,
    };
    ~RedisMgr();
    RedisMgr(const RedisMgr&) = delete;
    RedisMgr& operator = (const RedisMgr&) = delete;
    bool Get(const std::string& key, std::string& value);
    bool Set(const std::string& key, const std::string& value);
    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);
    bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
    std::string HGet(const std::string& key, const std::string& hkey);
    bool Del(const std::string& key);
    bool ExistsKey(const std::string& key);
    VerificationResult ConsumeVerificationCode(
        const std::string& email, const std::string& submitted_code, int max_attempts = 5);
    void Close();
private:
    RedisMgr();
    std::unique_ptr<RedisConPool> _con_pool;
};
