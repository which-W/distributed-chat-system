#pragma once
#include "ConfigMgr.h"
#include "Singleton.h"
#include "const.h"
#include "message.grpc.pb.h"
#include "message.pb.h"
#include <atomic>
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <queue>
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using message::GetVarifyReq;
using message::GetVarifyRsp;
using message::VarifyService;

class RPConPool {
  public:
    RPConPool(size_t poolSize, std::string host, std::string port);

    ~RPConPool();

    std::unique_ptr<VarifyService::Stub> getConnection();

    void returnConnection(std::unique_ptr<VarifyService::Stub> context);

    void Close();

  private:
    std::atomic<bool> b_stop_;
    size_t poolSize_;
    std::string host_;
    std::string port_;
    std::queue<std::unique_ptr<VarifyService::Stub>> connections_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class VerifyGrpcClient : public Singleton<VerifyGrpcClient> {
    friend class Singleton<VerifyGrpcClient>;

  public:
    GetVarifyRsp GetVarifyCode(std::string email, const std::string& request_id = {});

  private:
    VerifyGrpcClient();
    std::unique_ptr<RPConPool> _pool;
    std::string auth_token_;
};
