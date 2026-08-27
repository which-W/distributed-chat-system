#include "CServer.h"
#include <chrono>
#include <iostream>
#include "AsioIOServicePool.h"
#include "UserMgr.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"

CServer::CServer(boost::asio::io_context& io_context, const std::string& listen_host, unsigned short port)
    : _io_context(io_context), _port(port),
      _acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address(listen_host), port)),
      _timer(_io_context)
{
	cout << "Chat server is listening on " << listen_host << ':' << _port << endl;

	StartAccept();
}

CServer::~CServer() {
	cout << "Server destruct listen on port : " << _port << endl;

}

void CServer::HandleAccept(shared_ptr<CSession> new_session, const boost::system::error_code& error) {
	if (!error) {
		bool accepted = false;
		{
			lock_guard<mutex> lock(_mutex);
			// 在创建更多异步读取前执行全局连接预算，防止连接洪泛耗尽内存和句柄。
			if (_sessions.size() < MAX_CHAT_SESSIONS) {
				_sessions.insert(make_pair(new_session->GetSessionId(), new_session));
				accepted = true;
			}
		}
		if (accepted) new_session->Start();
		else new_session->Close();
	}
	else {
		cout << "session accept failed, error is " << error.what() << endl;
	}

	StartAccept();
}

void CServer::StartAccept() {
	auto& io_context = AsioIOServicePool::GetInstance()->GetIOServer();
	shared_ptr<CSession> new_session = make_shared<CSession>(io_context, this);
	_acceptor.async_accept(new_session->GetSocket(), std::bind(&CServer::HandleAccept, this, new_session, placeholders::_1));
}

//根据session 的id删除session，并移除用户和session的关联
void CServer::ClearSession(std::string session_id) {

	lock_guard<mutex> lock(_mutex);
	if (_sessions.find(session_id) != _sessions.end()) {
		auto uid = _sessions[session_id]->GetUserId();

		//移除用户和session的关联
		if (uid > 0) UserMgr::GetInstance()->RmvUserSession(uid);
	}

	_sessions.erase(session_id);

}

//根据用户获取session
shared_ptr<CSession> CServer::GetSession(std::string uuid) {
	lock_guard<mutex> lock(_mutex);
	auto it = _sessions.find(uuid);
	if (it != _sessions.end()) {
		return it->second;
	}
	return nullptr;
}

bool CServer::CheckValid(std::string uuid)
{
	lock_guard<mutex> lock(_mutex);
	auto it = _sessions.find(uuid);
	if (it != _sessions.end()) {
		return true;
	}
	return false;
}

void CServer::on_timer(const boost::system::error_code& ec) {
	if (ec == boost::asio::error::operation_aborted) {
		return;
	}
	if (ec) {
		std::cerr << "Chat health timer failed: " << ec.message() << std::endl;
	}
	else {
		PublishHealth();
	}

	_timer.expires_after(std::chrono::seconds(CHAT_HEARTBEAT_INTERVAL_SECONDS));
	auto self = shared_from_this();
	_timer.async_wait([self](const boost::system::error_code& error) {
		self->on_timer(error);
	});
}

void CServer::StartTimer()
{
	PublishHealth();
	_timer.expires_after(std::chrono::seconds(CHAT_HEARTBEAT_INTERVAL_SECONDS));
	auto self = shared_from_this();
	_timer.async_wait([self](const boost::system::error_code& error) {
		self->on_timer(error);
	});
}

void CServer::StopTimer()
{
	_timer.cancel();
	auto& cfg = ConfigMgr::Inst();
	RedisMgr::GetInstance()->Del(CHAT_HEALTH_PREFIX + cfg["SelfServer"]["Name"]);
}

void CServer::PublishHealth()
{
	std::size_t session_count = 0;
	{
		lock_guard<mutex> lock(_mutex);
		session_count = _sessions.size();
	}
	auto& cfg = ConfigMgr::Inst();
	const auto key = CHAT_HEALTH_PREFIX + cfg["SelfServer"]["Name"];
	if (!RedisMgr::GetInstance()->SetWithTtl(
			key, std::to_string(session_count), CHAT_HEARTBEAT_TTL_SECONDS)) {
		std::cerr << "Failed to publish chat health for "
			<< cfg["SelfServer"]["Name"] << std::endl;
	}
}
