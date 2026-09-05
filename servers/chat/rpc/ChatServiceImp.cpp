#include "ChatServiceImp.h"
#include "UserMgr.h"
#include "CSession.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "ConfigMgr.h"
#include "InternalRpcAuth.h"

namespace {

bool validProfileNotification(const AddFriendReq& request) {
	return request.name().size() <= 128 && request.nick().size() <= 128
		&& request.desc().size() <= 1024 && request.icon().size() <= 2048;
}

bool validTextNotification(const TextChatMsgReq& request) {
	if (request.textmsgs_size() <= 0
		|| request.textmsgs_size() > MAX_TEXT_MESSAGES_PER_FRAME) return false;
	std::size_t aggregate_bytes = 0;
	for (const auto& message : request.textmsgs()) {
		if (message.msgcontent().size() > MAX_TEXT_CONTENT_BYTES
			|| message.msgid().size() > MAX_TEXT_MESSAGE_ID_BYTES) return false;
		aggregate_bytes += message.msgcontent().size() + message.msgid().size();
		// JSON 转义可能扩大正文，因此原始字段预算显著小于最终 16 位帧上限。
		if (aggregate_bytes > 8192) return false;
	}
	return true;
}

} // namespace

ChatServiceImp::~ChatServiceImp()
{
}

ChatServiceImp::ChatServiceImp()
{

}

Status ChatServiceImp::NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* reply)
{
	const auto auth = chat::internal_rpc::authorize(
		*context, ConfigMgr::Inst()["InternalRpc"]["PeerToken"]);
	if (!auth.ok()) return auth;
	if (request->applyuid() <= 0 || request->touid() <= 0
		|| !validProfileNotification(*request)
		|| !MysqlMgr::GetInstance()->HasPendingFriendApply(request->applyuid(), request->touid())) {
		return Status(grpc::StatusCode::PERMISSION_DENIED, "friend application is not pending");
	}
	//查找是否有相关的用户
	auto touid = request->touid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	Defer defer([request, reply]() {
		reply->set_error(ErrorCodes::ERROR_CODE_OK);
		reply->set_applyuid(request->applyuid());
		reply->set_touid(request->touid());
		});

	if (!session) {
		return Status::OK;
	}


	//在内存中则直接发送通知对方
	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
	rtvalue["applyuid"] = request->applyuid();
	rtvalue["name"] = request->name();
	rtvalue["desc"] = request->desc();
	rtvalue["icon"] = request->icon();
	rtvalue["sex"] = request->sex();
	rtvalue["nick"] = request->nick();

	std::string return_str = rtvalue.toStyledString();

		if (!session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ)) {
			return Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "session send queue is full");
		}
	return Status::OK;

}

Status ChatServiceImp::NotifyAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* reply)
{
	const auto auth = chat::internal_rpc::authorize(
		*context, ConfigMgr::Inst()["InternalRpc"]["PeerToken"]);
	if (!auth.ok()) return auth;
	if (request->fromuid() <= 0 || request->touid() <= 0
		|| !MysqlMgr::GetInstance()->AreFriends(request->fromuid(), request->touid())) {
		return Status(grpc::StatusCode::PERMISSION_DENIED, "friend relation is not accepted");
	}
	//查找用户是否在本服务器
	auto touid = request->touid();
	auto fromuid = request->fromuid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	Defer defer([request, reply]() {
		reply->set_error(ErrorCodes::ERROR_CODE_OK);
		reply->set_fromuid(request->fromuid());
		reply->set_touid(request->touid());
		});

	//用户不在内存中则直接返回
	if (session == nullptr) {
		return Status::OK;
	}

	//在内存中则直接发送通知对方
	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
	rtvalue["fromuid"] = request->fromuid();
	rtvalue["touid"] = request->touid();

	std::string base_key = USER_BASE_INFO + std::to_string(fromuid);
	auto user_info = std::make_shared<UserInfo>();
	bool b_info = GetBaseInfo(base_key, fromuid, user_info);
	if (b_info) {
		rtvalue["name"] = user_info->name;
		rtvalue["nick"] = user_info->nick;
		rtvalue["icon"] = user_info->icon;
		rtvalue["sex"] = user_info->sex;
	}
	else {
		rtvalue["error"] = ErrorCodes::UidInvalid;
	}

	std::string return_str = rtvalue.toStyledString();

		if (!session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ)) {
			return Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "session send queue is full");
		}
	return Status::OK;
}

Status ChatServiceImp::NotifyTextChatMsg(::grpc::ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* reply)
{
		const auto auth = chat::internal_rpc::authorize(
			*context, ConfigMgr::Inst()["InternalRpc"]["PeerToken"]);
		if (!auth.ok()) return auth;
		if (request->fromuid() <= 0 || request->touid() <= 0
			|| !validTextNotification(*request)
			|| !MysqlMgr::GetInstance()->AreFriends(request->fromuid(), request->touid())) {
			return Status(grpc::StatusCode::PERMISSION_DENIED, "users are not accepted friends");
		}
		//查找用户是否在本服务器
		auto touid = request->touid();
		auto session = UserMgr::GetInstance()->GetSession(touid);
		reply->set_error(ErrorCodes::ERROR_CODE_OK);

		//用户不在内存中则直接返回
		if (session == nullptr) {
			return Status::OK;
		}

		//在内存中则直接发送通知对方
		Json::Value  rtvalue;
		rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
		rtvalue["fromuid"] = request->fromuid();
		rtvalue["touid"] = request->touid();

		//将聊天数据组织为数组
		Json::Value text_array;
		for (auto& msg : request->textmsgs()) {
			Json::Value element;
			element["content"] = msg.msgcontent();
			element["msgid"] = msg.msgid();
			text_array.append(element);
		}
		rtvalue["text_array"] = text_array;

		std::string return_str = rtvalue.toStyledString();
		if (return_str.size() > MAX_OUTBOUND_FRAME_BYTES) {
			return Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "chat frame is too large");
		}

			if (!session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ)) {
				reply->set_error(ErrorCodes::RPC_ERROR);
				return Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "session send queue is full");
			}
		return Status::OK;
}

bool ChatServiceImp::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
		if (b_base) {
			Json::Reader reader;
			Json::Value root;
			if (reader.parse(info_str, root) && root.isObject()
				&& root["uid"].isInt() && root["uid"].asInt() == uid
				&& root["name"].isString() && root["email"].isString()
				&& root["nick"].isString() && root["desc"].isString()
				&& root["sex"].isInt() && root["icon"].isString()) {
				userinfo->uid = uid;
				userinfo->name = root["name"].asString();
				userinfo->email = root["email"].asString();
				userinfo->nick = root["nick"].asString();
				userinfo->desc = root["desc"].asString();
				userinfo->sex = root["sex"].asInt();
				userinfo->icon = root["icon"].asString();
				return true;
			}
			RedisMgr::GetInstance()->Del(base_key);
		}
		{
		//redis中没有则查询mysql
		//查询数据库
		std::shared_ptr<UserInfo> user_info = nullptr;
		user_info = MysqlMgr::GetInstance()->GetUser(uid);
		if (user_info == nullptr) {
			return false;
		}

		userinfo = user_info;

		//将数据库内容写入redis缓存
		Json::Value redis_root;
		redis_root["uid"] = uid;
		redis_root["name"] = userinfo->name;
		redis_root["email"] = userinfo->email;
		redis_root["nick"] = userinfo->nick;
		redis_root["desc"] = userinfo->desc;
		redis_root["sex"] = userinfo->sex;
		redis_root["icon"] = userinfo->icon;
		RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	}
	return true;
}

Status ChatServiceImp::NotifyFileAvailable(::grpc::ServerContext* context,
	const message::FileAvailableReq* request, message::FileAvailableRsp* response)
{
	const auto auth = chat::internal_rpc::authorize(*context,
		ConfigMgr::Inst()["InternalRpc"]["PeerToken"]);
	if (!auth.ok()) return auth;
	const auto stored = MysqlMgr::GetInstance()->GetFileTransfer(request->id());
	if (!stored || stored->sender_uid != request->fromuid() || stored->receiver_uid != request->touid()
		|| stored->status != chat::files::TransferStatus::Available) {
		return Status(grpc::StatusCode::PERMISSION_DENIED, "file notification is not available");
	}
	response->set_error(ErrorCodes::ERROR_CODE_OK);
	if (auto session = UserMgr::GetInstance()->GetSession(request->touid())) {
		// 跨节点请求只携带定位信息；展示字段必须以数据库中的可信元数据为准。
		Json::Value value;
		value["error"]=0;value["id"]=stored->id;value["fromuid"]=stored->sender_uid;
		value["touid"]=stored->receiver_uid;value["name"]=stored->original_name;
		value["mime"]=stored->mime_type;value["total_size"]=Json::UInt64(stored->total_size);
		value["sha256"]=stored->sha256;
			if (!session->Send(value.toStyledString(), ID_NOTIFY_FILE_REQ)) {
				response->set_error(ErrorCodes::RPC_ERROR);
				return Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "session send queue is full");
			}
	}
	return Status::OK;
}

void ChatServiceImp::RegisterServer(std::shared_ptr<CServer> pServer)
{
	_p_server = pServer;
}
