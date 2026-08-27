#include "RedisMgr.h"
#include <chrono>
#include <cstring>

RedisMgr::RedisMgr() {
    auto& gCfgMgr = ConfigMgr::ins();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto user = gCfgMgr["Redis"]["User"];
    auto pwd = gCfgMgr["Redis"]["Passwd"];
    // Redis ACL 用户名必须与口令一起传入，不能静默退化为 default 用户认证。
    _con_pool.reset(new RedisConPool(5, host, atoi(port.c_str()), user, pwd));
}

RedisMgr::~RedisMgr()
{
    Close();
}

bool RedisMgr::Get(const std::string& key, std::string& value)
{
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());
    if (reply == NULL) {
        std::cout << "[ GET  " << key << " ] failed" << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        std::cout << "[ GET  " << key << " ] failed" << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    value = reply->str;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    std::cout << "Succeed to execute command [ GET " << key << "  ]" << std::endl;
    return true;
}

bool RedisMgr::Set(const std::string& key, const std::string& value)
{
    //执行redis命令行
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "SET %s %s", key.c_str(), value.c_str());

    //如果返回NULL则说明执行失败
    if (NULL == reply)
    {
        std::cout << "Redis SET failed for key " << key << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    //如果执行失败则释放连接
    if (!(reply->type == REDIS_REPLY_STATUS && (strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0)))
    {
        std::cout << "Redis SET failed for key " << key << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    //执行成功 释放redisCommand执行后返回的redisReply所占用的内存
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    std::cout << "Redis SET succeeded for key " << key << std::endl;
    return true;
}

bool RedisMgr::LPush(const std::string& key, const std::string& value)
{
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "LPUSH %s %s", key.c_str(), value.c_str());
    if (NULL == reply)
    {
        std::cout << "Redis LPUSH failed for key " << key << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        std::cout << "Redis LPUSH failed for key " << key << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    std::cout << "Redis LPUSH succeeded for key " << key << std::endl;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    return true;
}

bool RedisMgr::LPop(const std::string& key, std::string& value)
{
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "LPOP %s ", key.c_str());
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL) {
        std::cout << "Execut command [ LPOP " << key << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }
    value = reply->str;
    std::cout << "Execut command [ LPOP " << key << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value)
{
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str());
    if (NULL == reply)
    {
        std::cout << "Redis RPUSH failed for key " << key << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        std::cout << "Redis RPUSH failed for key " << key << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    std::cout << "Redis RPUSH succeeded for key " << key << std::endl;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    return true;
}

bool RedisMgr::RPop(const std::string& key, std::string& value)
{
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "RPOP %s ", key.c_str());
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL) {
        std::cout << "Execut command [ RPOP " << key << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }
    value = reply->str;
    std::cout << "Execut command [ RPOP " << key << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    return true;
}

bool RedisMgr::HSet(const std::string& key, const std::string& hkey, const std::string& value) {
    //二级set
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "HSET %s %s %s", key.c_str(), hkey.c_str(), value.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "Redis HSET failed for key " << key << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }
    std::cout << "Redis HSET succeeded for key " << key << std::endl;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    return true;
}

bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen)
{
    //二级set
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    const char* argv[4];
    size_t argvlen[4];
    argv[0] = "HSET";
    argvlen[0] = 4;
    argv[1] = key;
    argvlen[1] = strlen(key);
    argv[2] = hkey;
    argvlen[2] = strlen(hkey);
    argv[3] = hvalue;
    argvlen[3] = hvaluelen;
    auto reply = (redisReply*)redisCommandArgv(connect, 4, argv, argvlen);
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "Redis HSET failed for key " << key << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }
    std::cout << "Redis HSET succeeded for key " << key << std::endl;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    return true;
}

std::string RedisMgr::HGet(const std::string& key, const std::string& hkey)
{
    const char* argv[3];
    size_t argvlen[3];
    argv[0] = "HGET";
    argvlen[0] = 4;
    argv[1] = key.c_str();
    argvlen[1] = key.length();
    argv[2] = hkey.c_str();
    argvlen[2] = hkey.length();
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return "";
    }
    auto reply = (redisReply*)redisCommandArgv(connect, 3, argv, argvlen);
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        std::cout << "Execut command [ HGet " << key << " " << hkey << "  ] failure ! " << std::endl;
        return "";
    }

    std::string value = reply->str;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    std::cout << "Execut command [ HGet " << key << " " << hkey << " ] success ! " << std::endl;
    return value;
}

bool RedisMgr::Del(const std::string& key)
{
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "DEL %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "Execut command [ Del " << key << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }
    std::cout << "Execut command [ Del " << key << " ] success ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    return true;
}

bool RedisMgr::ExistsKey(const std::string& key)
{
    auto connect = _con_pool->getContext();
    if (connect == nullptr) {
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "exists %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER || reply->integer == 0) {
        std::cout << "Not Found [ Key " << key << " ]  ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }
    std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    return true;
}

void RedisMgr::Close()
{
    _con_pool->Close();
};

RedisMgr::VerificationResult RedisMgr::ConsumeVerificationCode(
    const std::string& email, const std::string& submitted_code, int max_attempts)
{
    static constexpr const char* script = R"lua(
local expected = redis.call('GET', KEYS[1])
if not expected then
  return -1
end
if expected ~= ARGV[1] then
  local attempts = redis.call('INCR', KEYS[2])
  if attempts == 1 then
    local ttl = redis.call('TTL', KEYS[1])
    if ttl < 1 then ttl = 300 end
    redis.call('EXPIRE', KEYS[2], ttl)
  end
  if attempts >= tonumber(ARGV[2]) then
    redis.call('DEL', KEYS[1])
    redis.call('DEL', KEYS[2])
    return -3
  end
  return -2
end
redis.call('DEL', KEYS[1])
redis.call('DEL', KEYS[2])
return 1
)lua";

    auto* connection = _con_pool->getContext();
    if (connection == nullptr) {
        return VerificationResult::RedisError;
    }

    const std::string code_key = CODE_HEAD + email;
    const std::string attempts_key = "code_attempts_" + email;
    const std::string max_attempts_text = std::to_string(max_attempts);
    const char* argv[] = {
        "EVAL", script, "2", code_key.c_str(), attempts_key.c_str(),
        submitted_code.c_str(), max_attempts_text.c_str()};
    const size_t argvlen[] = {
        4, std::strlen(script), 1, code_key.size(), attempts_key.size(),
        submitted_code.size(), max_attempts_text.size()};

    auto* reply = static_cast<redisReply*>(redisCommandArgv(connection, 7, argv, argvlen));
    _con_pool->returnContext(connection);
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        return VerificationResult::RedisError;
    }

    const auto result = reply->integer;
    freeReplyObject(reply);
    if (result == 1) return VerificationResult::Success;
    if (result == -1) return VerificationResult::Expired;
    if (result == -2) return VerificationResult::Mismatch;
    if (result == -3) return VerificationResult::TooManyAttempts;
    return VerificationResult::RedisError;
}
