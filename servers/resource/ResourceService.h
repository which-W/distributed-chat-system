#pragma once
#include "ResourceSupport.h"
#include "EncryptedFileStore.h"
#include "ResourceLock.h"
#include "message.grpc.pb.h"
#include "KeyedExecutor.h"
#include <boost/beast/http.hpp>

namespace resource {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
struct Response {
    unsigned status = 200; std::string body; std::string content_type = "application/json";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string stream_id; std::uint64_t begin = 0, length = 0;
};
class Service : public message::ResourceService::Service {
public:
    Service();
    Response handle(const Request&);
    std::vector<unsigned char> read(const std::string&, std::uint64_t);
    void maintenance();
    void stopNotifications();
    bool ready();
    grpc::Status GetMetadata(grpc::ServerContext*, const message::ResourceMetadataReq*, message::ResourceMetadataRsp*) override;
    grpc::Status ListPending(grpc::ServerContext*, const message::ResourcePendingReq*, message::ResourcePendingRsp*) override;
private:
    Database database_; Redis redis_; std::filesystem::path root_; EncryptedFileStore store_;
    struct NotificationEvent { std::string id, kind; Json::Value payload; };
    std::vector<std::shared_ptr<grpc::Channel>> peer_channels_;
    chat::runtime::KeyedExecutor notification_workers_;
    void deliverNotification(NotificationEvent event);
    Json::Value file(sql::Connection&, const std::string&);
    Json::Value create(int, const Json::Value&);
    Json::Value chunk(int, const std::string&, std::uint64_t, const std::string&);
    Json::Value complete(int, const std::string&);
    Response avatar(int, const Request&);
    void notify(sql::Connection&, const std::string&, const std::string&, const Json::Value&);
};
}
