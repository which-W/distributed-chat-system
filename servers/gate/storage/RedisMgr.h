#pragma once
#include "ConfigMgr.h"
#include "RedisConnectionPool.h"
#include "Singleton.h"
#include "const.h"
#include <memory>
#include <string>

using RedisConPool = chat::storage::RedisConnectionPool;

class RedisMgr : public Singleton<RedisMgr>, std::enable_shared_from_this<RedisMgr> {
    friend class Singleton<RedisMgr>;

  public:
    enum class VerificationResult {
        Success,
        Expired,
        Mismatch,
        TooManyAttempts,
        RedisError,
    };
    enum class SessionReadResult { Found, Missing, RedisError };
    enum class ResetGrantResult { Created, Exists, Expired, Mismatch, TooManyAttempts, RedisError };
    ~RedisMgr();
    RedisMgr(const RedisMgr&) = delete;
    RedisMgr& operator=(const RedisMgr&) = delete;
    bool Get(const std::string& key, std::string& value);
    bool Set(const std::string& key, const std::string& value);
    bool SetWithTtl(const std::string& key, const std::string& value, int ttl_seconds);
    SessionReadResult ReadSession(const std::string& key, std::string& value);
    SessionReadResult RenewSession(const std::string& key, const std::string& expected,
                                   int ttl_seconds);
    ResetGrantResult ConsumeResetCodeAndGrant(const std::string& email,
                                               const std::string& submitted_code,
                                               const std::string& grant_key,
                                               const std::string& payload);
    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);
    bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
    std::string HGet(const std::string& key, const std::string& hkey);
    bool Del(const std::string& key);
    bool ExistsKey(const std::string& key);
    VerificationResult ConsumeVerificationCode(const std::string& email,
                                               const std::string& submitted_code,
                                               int max_attempts = 5);
    void Close();

  private:
    RedisMgr();
    std::unique_ptr<RedisConPool> _con_pool;
};
