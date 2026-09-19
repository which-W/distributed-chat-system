#pragma once
#include "ConfigMgr.h"
#include "FileTransferTypes.h"
#include <optional>
#include "GrpcTlsSupport.h"
#include "InternalRpcAuth.h"
#include "message.grpc.pb.h"
#include <json/json.h>
#include <sstream>

inline Json::Value PendingResources(int uid) {
    auto& cfg = ConfigMgr::Inst();
    auto endpoints = cfg["Resource"]["Endpoints"];
    if (endpoints.empty()) endpoints = "127.0.0.1:50065,127.0.0.1:50066";
    std::istringstream input(endpoints); std::string endpoint;
    while (std::getline(input, endpoint, ',')) {
        auto separator = endpoint.rfind(':'); if (separator == std::string::npos) continue;
        auto stub = message::ResourceService::NewStub(chat::grpc_tls::make_channel(endpoint.substr(0,separator),endpoint.substr(separator+1),chat::grpc_tls::from_config(cfg)));
        grpc::ClientContext context; context.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(2));
        chat::internal_rpc::authenticate(context,cfg["InternalRpc"]["PeerToken"]);
        message::ResourcePendingReq request; request.set_uid(uid); message::ResourcePendingRsp response;
        if (!stub->ListPending(&context,request,&response).ok()) continue;
        Json::Value values(Json::arrayValue);
        for (const auto& data : response.metadata_json()) {
            Json::Value value; Json::Reader reader;
            if (!reader.parse(data,value)) throw std::runtime_error("invalid resource response"); values.append(value);
        }
        return values;
    }
    // Caller must expose unavailable state, not treat it as an empty resource list.
    return Json::Value();
}
inline std::optional<chat::files::TransferRecord> ResourceMetadata(const std::string& id, int uid) {
    auto& cfg=ConfigMgr::Inst(); auto endpoints=cfg["Resource"]["Endpoints"];
    if (endpoints.empty()) endpoints="127.0.0.1:50065,127.0.0.1:50066";
    std::istringstream input(endpoints); std::string endpoint;
    while (std::getline(input,endpoint,',')) {
        auto colon=endpoint.rfind(':'); if (colon==std::string::npos) continue;
        auto stub=message::ResourceService::NewStub(chat::grpc_tls::make_channel(endpoint.substr(0,colon),endpoint.substr(colon+1),chat::grpc_tls::from_config(cfg)));
        grpc::ClientContext context; context.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(2));
        chat::internal_rpc::authenticate(context,cfg["InternalRpc"]["PeerToken"]);
        message::ResourceMetadataReq req; req.set_id(id); req.set_uid(uid); message::ResourceMetadataRsp rsp;
        if (!stub->GetMetadata(&context,req,&rsp).ok()) continue;
        Json::Value v; Json::Reader reader; if (!reader.parse(rsp.metadata_json(),v)) return {};
        chat::files::TransferRecord f; f.id=v["id"].asString(); f.sender_uid=v["fromuid"].asInt(); f.receiver_uid=v["touid"].asInt();
        f.original_name=v["name"].asString(); f.mime_type=v["mime"].asString(); f.total_size=v["total_size"].asUInt64(); f.sha256=v["sha256"].asString();
        f.status=v["status"]=="available" ? chat::files::TransferStatus::Available : (v["status"]=="downloaded" ? chat::files::TransferStatus::Downloaded : chat::files::TransferStatus::Expired);
        return f;
    }
    return {};
}
