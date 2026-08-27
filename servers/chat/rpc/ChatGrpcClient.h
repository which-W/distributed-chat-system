#pragma once
#include "const.h"
#include "ConfigMgr.h"
#include "Singleton.h"
#include "message.grpc.pb.h"
#include "message.pb.h"
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <queue>
#include <mutex>
#include "data.h"
#include <unordered_map>
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "GrpcTlsSupport.h"
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using message::ChatService;

using message::AddFriendReq;
using message::AddFriendRsp;

using message::GetChatServerReq;
using message::GetChatServerRsp;

using message::LoginReq;
using message::LoginRsp;

using message::RplyFriendReq;
using message::RplyFriendRsp;

using message::AuthFriendReq;
using message::AddFriendRsp;

using message::TextChatMsgReq;
using message::TextChatMsgRsp;

using message::AuthFriendRsp;
using message::AuthFriendReq;

using message::TextChatData;

class ChatConPool {

public:
	ChatConPool(size_t pool_size, std::string host, std::string port,
		const chat::grpc_tls::Options& tls, const std::string& tls_name)
		: pool_size_(pool_size), host_(host), port_(port), b_stop_(false) {
		for (size_t i = 0; i < pool_size_; ++i) {
			std::shared_ptr<Channel> channel = chat::grpc_tls::make_channel(host, port, tls, tls_name);
			connections_.push(ChatService::NewStub(channel));
		}
	};
	~ChatConPool() {
		std::lock_guard<std::mutex> lock(mutex_);
		Close();
		while (!connections_.empty()) {
			connections_.pop();
		}
	};

	std::unique_ptr<ChatService::Stub> Get_connection() {
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [this] {
			if (b_stop_) {
				return true;
			}
			return !connections_.empty();
			});
		//如果停止则直接返回空指针
		if (b_stop_) {
			return nullptr;
		}
		auto context = std::move(connections_.front());
		connections_.pop();
		return context;
	}

	void Return_connection(std::unique_ptr<ChatService::Stub> context) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (b_stop_) {
			return;
		}
		connections_.push(std::move(context));
		cond_.notify_one();
	}

	void Close() {
		b_stop_ = true;
		cond_.notify_all();

	};

private:
	size_t pool_size_;
	std::string host_;
	std::string port_;
	std::queue<std::unique_ptr<ChatService::Stub>> connections_;
	std::mutex mutex_;
	std::condition_variable cond_;
	bool b_stop_;

};



class ChatGrpcClient : public Singleton<ChatGrpcClient>
{
	friend class Singleton<ChatGrpcClient>;
public:
	~ChatGrpcClient() {};

	AddFriendRsp NotifyAddFriend(std::string server_ip, const AddFriendReq& req);
	AuthFriendRsp NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req);
	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);
	TextChatMsgRsp NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req, const Json::Value& rtvalue);
	message::FileAvailableRsp NotifyFileAvailable(
		std::string server_ip, const message::FileAvailableReq& req);
private:
	ChatGrpcClient();
	std::unordered_map<std::string, std::unique_ptr<ChatConPool>> _pools;
	std::string _auth_token;
};
