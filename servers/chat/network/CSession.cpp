#include "CSession.h"
#include "CServer.h"
#include <iostream>
#include <sstream>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "LogicSystem.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include <limits>

CSession::CSession(boost::asio::io_context& io_context, CServer* server):
	_socket(io_context), _server(server), _b_close(false),_b_head_parse(false), _user_uid(0),
	_auth_timer(io_context), _idle_timer(io_context){
	boost::uuids::uuid  a_uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(a_uuid);
	_recv_head_node = make_shared<MsgNode>(HEAD_TOTAL_LEN);
	_last_heartbeat = std::time(nullptr);
}

CSession::~CSession() {
	std::cout << "~CSession destruct" << endl;
}

tcp::socket& CSession::GetSocket() {
	return _socket;
}

std::string& CSession::GetSessionId() {
	return _session_id;
}

void CSession::SetUserId(int uid)
{
	_user_uid.store(uid);
	if (uid > 0) {
		// 票据验证成功后取消预认证截止时间，避免已认证连接被旧定时器误关闭。
		_auth_timer.cancel();
		TouchActivity();
	}
}

int CSession::GetUserId()
{
	return _user_uid.load();
}

void CSession::Start(){
	_auth_timer.expires_after(std::chrono::seconds(CHAT_AUTH_TIMEOUT_SECONDS));
	auto self = shared_from_this();
	_auth_timer.async_wait([self](const boost::system::error_code& error) {
		if (!error && self->GetUserId() <= 0) {
			std::cerr << "Chat session authentication timed out" << std::endl;
			self->Close();
		}
	});
	AsyncReadHead(HEAD_TOTAL_LEN);
}

bool CSession::Send(std::string msg, short msgid) {
	// 帧头只有 16 位长度；任何窄化前都必须硬拒绝，避免负长度转巨量分配。
	if (msg.size() > std::numeric_limits<std::uint16_t>::max()) {
		Close();
		return false;
	}
	if (_b_close.load() || !_socket.is_open()) return false;
	std::lock_guard<std::mutex> lock(_send_lock);
	int send_que_size = _send_que.size();
    if (send_que_size >= MAX_SENDQUE) {
		std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
		return false;
	}

	_send_que.push(make_shared<SendNode>(msg.c_str(), msg.length(), msgid));
	if (send_que_size > 0) {
		return true;
	}
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
	return true;
}

bool CSession::Send(char* msg, std::size_t max_length, short msgid) {
	if (max_length > std::numeric_limits<std::uint16_t>::max()) {
		Close();
		return false;
	}
	if (_b_close.load() || !_socket.is_open()) return false;
	std::lock_guard<std::mutex> lock(_send_lock);
	int send_que_size = _send_que.size();
	if (send_que_size >= MAX_SENDQUE) {
		std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
		return false;
	}

	_send_que.push(make_shared<SendNode>(msg, max_length, msgid));
	if (send_que_size>0) {
		return true;
	}
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
	return true;
}

void CSession::Close() {
	{
		std::lock_guard<std::mutex> lock(_session_mtx);
		if (_b_close.exchange(true)) return;
		boost::system::error_code ignored;
		_auth_timer.cancel();
		_idle_timer.cancel();
		_socket.cancel(ignored);
		_socket.shutdown(tcp::socket::shutdown_both, ignored);
		_socket.close(ignored);
	}
	// 所有错误和超时统一走幂等清理路径，释放会话表中的 shared_ptr。
	_server->ClearSession(_session_id);
}

void CSession::TouchActivity()
{
	auto self = shared_from_this();
	boost::asio::post(_socket.get_executor(), [self]() { self->ScheduleIdleTimeout(); });
}

void CSession::ScheduleIdleTimeout()
{
	if (_b_close.load() || GetUserId() <= 0) return;
	_idle_timer.expires_after(std::chrono::seconds(CHAT_IDLE_TIMEOUT_SECONDS));
	auto self = shared_from_this();
	_idle_timer.async_wait([self](const boost::system::error_code& error) {
		if (!error) {
			std::cerr << "Chat session idle timeout" << std::endl;
			self->Close();
		}
	});
}

std::shared_ptr<CSession>CSession::SharedSelf() {
	return shared_from_this();
}

void CSession::AsyncReadBody(int total_len)
{
	auto self = shared_from_this();
	asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << endl;
				Close();
				return;
			}

			if (bytes_transfered < total_len) {
				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
					<< total_len<<"]" << endl;
				Close();
				return;
			}

			//判断连接无效
			if (!_server->CheckValid(_session_id)) {
				Close();
				return;
			}

			memcpy(_recv_msg_node->_data , _data , bytes_transfered);
			_recv_msg_node->_cur_len += bytes_transfered;
				_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
				TouchActivity();
			// 登录前只允许登录帧进入逻辑层，任何业务帧都立即释放连接。
			if (_recv_msg_node->GetRecMsgNodeID() != MSG_CHAT_LOGIN && GetUserId() <= 0) {
				Close();
				return;
			}
			// 上传任务只进入文件队列，避免同一帧在两个队列中重复占用内存。
			if (IsFileTransferMessage(_recv_msg_node->GetRecMsgNodeID())) {
				std::hash<std::string> hash_fn;
				size_t hash_value = hash_fn(_session_id); // 生成哈希值
				int index = hash_value % LOGIC_WORKER_COUNT;
				std::cout << "Hash value: " << hash_value << std::endl;
				if (!LogicSystem::GetInstance()->PostMsgToFileQue(
						make_shared<LogicNode>(shared_from_this(), _recv_msg_node), index)) {
					Close();
					return;
				}
			}
			else if (!LogicSystem::GetInstance()->PostMsgToQueue(
					make_shared<LogicNode>(shared_from_this(), _recv_msg_node))) {
				Close();
				return;
			}
			//继续监听头部接受事件
			AsyncReadHead(HEAD_TOTAL_LEN);
		}
		catch (std::exception& e) {
			std::cout << "Exception code is " << e.what() << endl;
			Close();
		}
		});
}

void CSession::AsyncReadHead(int total_len)
{
	auto self = shared_from_this();
	asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << endl;
				Close();
				return;
			}

			if (bytes_transfered < HEAD_TOTAL_LEN) {
				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
					<< HEAD_TOTAL_LEN << "]" << endl;
				Close();
				return;
			}

			//判断连接无效
			if (!_server->CheckValid(_session_id)) {
				Close();
				return;
			}

			_recv_head_node->Clear();
			memcpy(_recv_head_node->_data, _data, bytes_transfered);

			//获取头部MSGID数据
			std::uint16_t msg_id = 0;
			memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
			//网络字节序转化为本地字节序
			msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
			//id非法
				if (!IsClientRequestMessage(msg_id)) {
				std::cout << "invalid msg_id is " << msg_id << endl;
				Close();
				return;
			}
			std::uint16_t msg_len = 0;
			memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
			//网络字节序转化为本地字节序
			msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);

			//id非法
			const auto allowed_length = IsFileTransferMessage(msg_id)
				? MAX_FILE_FRAME_LENGTH : MAX_LENGTH;
			if (msg_len == 0 || msg_len > allowed_length) {
				std::cout << "invalid data length is " << msg_len << endl;
				Close();
				return;
			}

			_recv_msg_node = make_shared<RecvNode>(msg_len, msg_id);
			AsyncReadBody(msg_len);
		}
		catch (std::exception& e) {
			std::cout << "Exception code is " << e.what() << endl;
			Close();
		}
		});
}

void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self) {
	//增加异常处理
	try {
		auto self = shared_from_this();
		if (!error) {
			std::lock_guard<std::mutex> lock(_send_lock);
			//cout << "send data " << _send_que.front()->_data+HEAD_LENGTH << endl;
			_send_que.pop();
			if (!_send_que.empty()) {
				auto& msgnode = _send_que.front();
				boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
					std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self));
			}
		}
		else {
			std::cout << "handle write failed, error is " << error.what() << endl;
			Close();
		}
	}
	catch (std::exception& e) {
		std::cerr << "Exception code : " << e.what() << endl;
	}

}

//读取完整长度
void CSession::asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler )
{
	if (maxLength == 0 || maxLength > sizeof(_data)) {
		handler(make_error_code(boost::asio::error::message_size), 0);
		return;
	}
	asyncReadLen(0, maxLength, handler);
}

//读取指定字节数
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
	std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
	auto self = shared_from_this();
	_socket.async_read_some(boost::asio::buffer(_data + read_len, total_len-read_len),
		[read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t  bytesTransfered) {
			if (ec) {
				// 出现错误，调用回调函数
				handler(ec, read_len + bytesTransfered);
				return;
			}

			if (read_len + bytesTransfered >= total_len) {
				//长度够了就调用回调函数
				handler(ec, read_len + bytesTransfered);
				return;
			}

			// 没有错误，且长度不足则继续读取
			self->asyncReadLen(read_len + bytesTransfered, total_len, handler);
	});
}

void CSession::NotifyOffline(int uid) {

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
	rtvalue["uid"] = uid;


	std::string return_str = rtvalue.toStyledString();

	Send(return_str, ID_NOTIFY_OFF_LINE_REQ);
	return;
}

LogicNode::LogicNode(shared_ptr<CSession>  session,
	shared_ptr<RecvNode> recvnode):_session(session),_recvnode(recvnode) {

}


//bool CSession::IsHeartbeatExpired(std::time_t& now) {
//	double diff_sec = std::difftime(now, _last_heartbeat);
//	if (diff_sec > 20) {
//		std::cout << "heartbeat expired, session id is  " << _session_id << endl;
//		return true;
//	}
//
//	return false;
//}
//
//void CSession::UpdateHeartbeat()
//{
//	time_t now = std::time(nullptr);
//	_last_heartbeat = now;
//}
//
//void CSession::DealExceptionSession()
//{
//	auto self = shared_from_this();
//	//加锁清除session
//	auto uid_str = std::to_string(_user_uid);
//	auto lock_key = LOCK_PREFIX + uid_str;
//	auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
//	Defer defer([identifier, lock_key, self, this]() {
//		_server->ClearSession(_session_id);
//		RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
//		});
//
//	if (identifier.empty()) {
//		return;
//	}
//	std::string redis_session_id = "";
//	auto bsuccess = RedisMgr::GetInstance()->Get(USER_SESSION_PREFIX + uid_str, redis_session_id);
//	if (!bsuccess) {
//		return;
//	}
//
//	if (redis_session_id != _session_id) {
//		//说明有客户在其他服务器异地登录了
//		return;
//	}
//
//	RedisMgr::GetInstance()->Del(USER_SESSION_PREFIX + uid_str);
//	//清除用户登录信息
//	RedisMgr::GetInstance()->Del(USERIPPREFIX + uid_str);
//}
