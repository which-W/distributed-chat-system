#include "StatusGrpcClient.h"
#include "GrpcClientDeadline.h"

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid)
{
	    ClientContext context;
	chat::grpc_client::setDeadline(context, chat::grpc_client::kInternalRpcTimeout);
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);
	    auto stub = pool_->getConnection();
	if (!stub) { reply.set_error(ErrorCodes::RPC_ERROR); return reply; }
    Status status = stub->GetChatServer(&context, request, &reply);
    Defer defer([&stub, this]() {
        pool_->returnConnection(std::move(stub));
        });
    if (status.ok()) {
        return reply;
    }
    else {
        reply.set_error(ErrorCodes::RPC_ERROR);
        return reply;
    }
}
LoginRsp StatusGrpcClient::Login(int uid, const std::string& token)
{
	    ClientContext context;
	chat::grpc_client::setDeadline(context, chat::grpc_client::kInternalRpcTimeout);
    LoginRsp reply;
    LoginReq request;
    request.set_uid(uid);
    request.set_token(token);

	    auto stub = pool_->getConnection();
	if (!stub) { reply.set_error(ErrorCodes::RPC_ERROR); return reply; }
    Status status = stub->Login(&context, request, &reply);
    Defer defer([&stub, this]() {
        pool_->returnConnection(std::move(stub));
        });
    if (status.ok()) {
        return reply;
    }
    else {
        reply.set_error(ErrorCodes::RPC_ERROR);
        return reply;
    }
}
StatusGrpcClient::StatusGrpcClient()
{
    auto& gCfgMgr = ConfigMgr::Inst();
    std::string host = gCfgMgr["StatusServer"]["Host"];
    std::string port = gCfgMgr["StatusServer"]["Port"];
    pool_.reset(new StatusConPool(5, host, port, chat::grpc_tls::from_config(gCfgMgr),
        gCfgMgr["StatusServer"]["TLSName"]));
}
