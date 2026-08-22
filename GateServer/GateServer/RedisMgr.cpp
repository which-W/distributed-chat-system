#include "RedisMgr.h"

RedisMgr::RedisMgr() {
    auto& gCfgMgr = ConfigMgr::ins();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Passwd"];
    _con_pool.reset(new RedisConPool(5, host.c_str(), atoi(port.c_str()), pwd.c_str()));
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
        std::cout << "Execut command [ SET " << key << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    //如果执行失败则释放连接
    if (!(reply->type == REDIS_REPLY_STATUS && (strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0)))
    {
        std::cout << "Execut command [ SET " << key << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    //执行成功 释放redisCommand执行后返回的redisReply所占用的内存
    freeReplyObject(reply);
    _con_pool->returnContext(connect);
    std::cout << "Execut command [ SET " << key << "  " << value << " ] success ! " << std::endl;
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
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    std::cout << "Execut command [ LPUSH " << key << "  " << value << " ] success ! " << std::endl;
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
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }

    std::cout << "Execut command [ RPUSH " << key << "  " << value << " ] success ! " << std::endl;
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
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }
    std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << value << " ] success ! " << std::endl;
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
        std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] failure ! " << std::endl;
        freeReplyObject(reply);
        _con_pool->returnContext(connect);
        return false;
    }
    std::cout << "Execut command [ HSet " << key << "  " << hkey << "  " << hvalue << " ] success ! " << std::endl;
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

RedisConPool::RedisConPool(size_t pool_size, const char* host, int port, const char* pwd) :
    _pool_size(pool_size), _host(host), _port(port), _b_stop_(false)
{
    for (size_t i = 0; i < _pool_size; i++) {
        auto* context = redisConnect(host, port);
        if (context == nullptr || context->err != 0) {
            if (context != nullptr) {
                redisFree(context);
            }
            continue;
        }
        auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd);
        if (reply->type == REDIS_REPLY_ERROR) {
            std::cout << "认证失败" << std::endl;
            //执行成功 释放redisCommand执行后返回的redisReply所占用的内存
            freeReplyObject(reply);
            continue;
        }
        //执行成功 释放redisCommand执行后返回的redisReply所占用的内存
        freeReplyObject(reply);
        std::cout << "认证成功" << std::endl;
        //将已经建立好的连接压入队列中
        _redis_connections.emplace(context);

    }

}

RedisConPool::~RedisConPool()
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto* conn = _redis_connections.front();
    if (conn) {
        redisFree(conn);
    }
    _redis_connections.pop();
}

redisContext* RedisConPool::getContext()
{
    std::unique_lock<std::mutex> lock(_mutex);
    // 设置最大等待时间（例如 10 秒）
    auto timeout = std::chrono::seconds(30);
    bool ready = _cond.wait_for(lock, timeout, [this]() {
        return _b_stop_ || !_redis_connections.empty();
        });
    //如果停止则直接返回空指针
    if (!ready) {
        std::cerr << "获取 Redis 连接超时！" << std::endl;
        return nullptr;
    }
    if (_b_stop_) {
        return  nullptr;
    }
    auto* context = _redis_connections.front();
    _redis_connections.pop();
    return context;
}

void RedisConPool::returnContext(redisContext* context)
{
    if (_b_stop_) {
        return;
    }
    _redis_connections.emplace(context);
    _cond.notify_one();
}

void RedisConPool::Close()
{
    _b_stop_ = true;
    _cond.notify_all();
}
