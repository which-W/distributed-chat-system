#include "StatusServiceImpl.h"
#include "ChatLogger.h"
#include "ChatServerSelector.h"
#include "ConfigMgr.h"
#include "InternalRpcAuth.h"
#include "RedisMgr.h"
#include "const.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <stdexcept>

std::string generate_unique_string() {
    // 创建UUID对象
    boost::uuids::uuid uuid = boost::uuids::random_generator()();

    // 将UUID转换为字符串
    std::string unique_string = to_string(uuid);

    return unique_string;
}

Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request,
                                        GetChatServerRsp* reply) {
    const auto request_id = chat::internal_rpc::request_id(*context);
    const auto auth =
        chat::internal_rpc::authorize(*context, ConfigMgr::Inst()["InternalRpc"]["GateToken"]);
    if (!auth.ok()) {
        chat::observability::log(chat::observability::Level::Warn, "routing.request_rejected",
                                 "chat routing request was not authorized",
                                 {{"request_id", request_id}});
        return auth;
    }
    if (request->uid() <= 0) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT, "uid must be positive");
    }
    std::string prefix("StatusServer has received :  ");
    const auto server = getChatServer();
    if (!server.has_value()) {
        chat::observability::log(
            chat::observability::Level::Warn, "routing.no_healthy_server",
            "no healthy chat server is available",
            {{"request_id", request_id}, {"uid", std::int64_t(request->uid())}});
        reply->set_error(ErrorCodes::RPCFailed);
        return Status::OK;
    }
    reply->set_host(server->host);
    reply->set_port(server->port);
    reply->set_transport(server->transport);
    reply->set_tls_server_name(server->tls_server_name);
    reply->set_error(ErrorCodes::Success);
    reply->set_token(generate_unique_string());
    if (!insertToken(request->uid(), reply->token(), server->name)) {
        reply->set_error(ErrorCodes::RPCFailed);
        reply->clear_token();
    }
    chat::observability::log(chat::observability::Level::Info, "routing.server_selected",
                             "chat server selected",
                             {{"request_id", request_id},
                              {"uid", std::int64_t(request->uid())},
                              {"server", server->name},
                              {"connection_count", std::int64_t(server->con_count)},
                              {"status", std::int64_t(reply->error())}});
    return Status::OK;
}

StatusServiceImpl::StatusServiceImpl() {
    auto& cfg = ConfigMgr::Inst();
    auto server_list = cfg["chatservers"]["Name"];
    chat::observability::log(chat::observability::Level::Info, "routing.configuration_loaded",
                             "loading configured chat servers");
    std::vector<std::string> words;

    std::stringstream ss(server_list);
    std::string word;

    while (std::getline(ss, word, ',')) {
        word.erase(word.begin(), std::find_if(word.begin(), word.end(),
                                              [](unsigned char ch) { return !std::isspace(ch); }));
        word.erase(std::find_if(word.rbegin(), word.rend(),
                                [](unsigned char ch) { return !std::isspace(ch); })
                       .base(),
                   word.end());
        if (word.empty()) {
            continue;
        }
        words.push_back(word);
    }

    for (auto& word : words) {
        if (cfg[word]["Name"].empty()) {
            continue;
        }
        ChatServer server;
        server.port = cfg[word]["PublicPort"];
        if (server.port.empty()) {
            server.port = cfg[word]["Port"];
        }
        server.host = cfg[word]["PublicHost"];
        if (server.host.empty()) {
            server.host = cfg[word]["Host"];
        }
        server.name = cfg[word]["Name"];
        server.transport = cfg[word]["Transport"];
        if (server.transport.empty()) {
            server.transport = "insecure";
        }
        std::transform(server.transport.begin(), server.transport.end(), server.transport.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (server.transport != "tls" && server.transport != "insecure") {
            throw std::runtime_error("Chat server " + word + " has invalid Transport");
        }
        server.tls_server_name = cfg[word]["TLSName"];
        if (server.transport == "tls" && server.tls_server_name.empty()) {
            server.tls_server_name = server.host;
        }
        if (server.host.empty() || server.port.empty()) {
            throw std::runtime_error("Chat server " + word + " has no public host or port");
        }
        chat::observability::log(
            chat::observability::Level::Debug, "routing.server_configured",
            "chat server configured",
            {{"server", server.name}, {"host", server.host}, {"port", server.port}});
        _servers[server.name] = server;
    }
    if (_servers.empty()) {
        throw std::runtime_error("No valid chat servers are configured");
    }
}

std::optional<ChatServer> StatusServiceImpl::getChatServer() {
    std::lock_guard<std::mutex> guard(_server_mtx);
    if (_servers.empty()) {
        return std::nullopt;
    }

    std::vector<chat::routing::ServerLoad> healthy_servers;
    for (const auto& [name, server] : _servers) {
        std::string count_text;
        if (!RedisMgr::GetInstance()->Get(CHAT_HEALTH_PREFIX + name, count_text)) {
            continue;
        }
        int connection_count = 0;
        const auto parse = std::from_chars(count_text.data(), count_text.data() + count_text.size(),
                                           connection_count);
        if (parse.ec != std::errc{} || parse.ptr != count_text.data() + count_text.size() ||
            connection_count < 0) {
            continue;
        }
        healthy_servers.push_back({server.name, connection_count});
    }

    const auto selected_name =
        chat::routing::selectLeastLoaded(healthy_servers, _round_robin_cursor);
    if (!selected_name.has_value()) {
        return std::nullopt;
    }
    ++_round_robin_cursor;
    auto selected = _servers.at(*selected_name);
    const auto load =
        std::find_if(healthy_servers.begin(), healthy_servers.end(),
                     [&](const auto& candidate) { return candidate.name == *selected_name; });
    selected.con_count = load->connection_count;
    return selected;
}

Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply) {
    (void)context;
    (void)request;
    // Legacy utoken_* values had no expiry or replay protection. The chat server
    // now consumes the short-lived ticket issued by GetChatServer directly.
    reply->set_error(ErrorCodes::TokenInvalid);
    return Status::OK;
}

bool StatusServiceImpl::insertToken(int uid, const std::string& token,
                                    const std::string& server_name) {
    Json::Value ticket;
    ticket["uid"] = uid;
    ticket["server"] = server_name;
    const std::string token_key = CHAT_TICKET_PREFIX + token;
    return RedisMgr::GetInstance()->SetEx(token_key, ticket.toStyledString(),
                                          CHAT_TICKET_TTL_SECONDS);
}
