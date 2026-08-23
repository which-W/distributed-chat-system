#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include <climits>
#include <algorithm>
#include <cctype>
#include <stdexcept>

std::string generate_unique_string() {
	// 创建UUID对象
	boost::uuids::uuid uuid = boost::uuids::random_generator()();

	// 将UUID转换为字符串
	std::string unique_string = to_string(uuid);

	return unique_string;
}

Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
	std::string prefix("StatusServer has received :  ");
	const auto& server = getChatServer();
	reply->set_host(server.host);
	reply->set_port(server.port);
	reply->set_transport(server.transport);
	reply->set_tls_server_name(server.tls_server_name);
	reply->set_error(ErrorCodes::Success);
	reply->set_token(generate_unique_string());
	std::cout << request->uid() << std::endl;
	if (!insertToken(request->uid(), reply->token(), server.name)) {
		reply->set_error(ErrorCodes::RPCFailed);
		reply->clear_token();
	}
	return Status::OK;
}

StatusServiceImpl::StatusServiceImpl()
{
	auto& cfg = ConfigMgr::Inst();
	auto server_list = cfg["chatservers"]["Name"];
	std::cout << "server list" << server_list << std::endl;
	std::vector<std::string> words;

	std::stringstream ss(server_list);
	std::string word;

	while (std::getline(ss, word, ',')) {
		word.erase(word.begin(), std::find_if(word.begin(), word.end(), [](unsigned char ch) {
			return !std::isspace(ch);
			}));
		word.erase(std::find_if(word.rbegin(), word.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
			}).base(), word.end());
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
		std::cout << "server name: " << server.name << std::endl;
		_servers[server.name] = server;
	}
	if (_servers.empty()) {
		throw std::runtime_error("No valid chat servers are configured");
	}
}

ChatServer StatusServiceImpl::getChatServer() {
	std::lock_guard<std::mutex> guard(_server_mtx);
	if (_servers.empty()) {
		throw std::runtime_error("No chat servers are configured");
	}
	ChatServer minServer = _servers.begin()->second;
	std::string count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name);
	std::cout << "minServer name: " << minServer.name << std::endl;
	std::cout << "count_str is" << count_str << std::endl;
	if (count_str.empty()) {
		//不存在则默认设置为最大
		minServer.con_count = INT_MAX;
	}
	else {
		minServer.con_count = std::stoi(count_str);
	}


	// 使用范围基于for循环，寻找连接数最小的服务器
	for ( auto& server : _servers) {

		if (server.second.name == minServer.name) {
			continue;
		}

		auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name);
		if (count_str.empty()) {
			server.second.con_count = INT_MAX;
		}
		else {
			server.second.con_count = std::stoi(count_str);
		}

		if (server.second.con_count < minServer.con_count) {
			minServer = server.second;
		}
	}
	return minServer;
}

Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
	(void)context;
	(void)request;
	// Legacy utoken_* values had no expiry or replay protection. The chat server
	// now consumes the short-lived ticket issued by GetChatServer directly.
	reply->set_error(ErrorCodes::TokenInvalid);
	return Status::OK;
}

bool StatusServiceImpl::insertToken(
	int uid, const std::string& token, const std::string& server_name)
{
	Json::Value ticket;
	ticket["uid"] = uid;
	ticket["server"] = server_name;
	const std::string token_key = CHAT_TICKET_PREFIX + token;
	return RedisMgr::GetInstance()->SetEx(
		token_key, ticket.toStyledString(), CHAT_TICKET_TTL_SECONDS);
}
