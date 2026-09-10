#pragma once
#include "HttpConnection.h"
#include "RedisMgr.h"
#include "Singleton.h"
#include "VerifyGrpcClient.h"
#include "const.h"
class HttpConnection;
typedef std::function<void(std::shared_ptr<HttpConnection>)> HttpHandler;
class LogicSystem : public Singleton<LogicSystem> {
    friend class Singleton<LogicSystem>;

  public:
    ~LogicSystem();
    bool HandleGet(std::string, std::shared_ptr<HttpConnection>);
    void RegGet(std::string, HttpHandler handler);
    void RegPost(std::string, HttpHandler handler);
    bool HandlePost(std::string, std::shared_ptr<HttpConnection>);

  private:
    LogicSystem();
    std::unordered_map<std::string, HttpHandler> _pos_Handlers;
    std::unordered_map<std::string, HttpHandler> _get_Handlers;
};
