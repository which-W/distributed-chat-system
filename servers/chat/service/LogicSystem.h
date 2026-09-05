#pragma once
#include<thread>
#include<map>
#include<functional>
#include<iostream>
#include<queue>
#include"Singleton.h"
#include"CSession.h"
#include<json/reader.h>
#include<json/value.h>
#include<json/json.h>
#include "StatusGrpcClient.h"
#include "MysqlMgr.h"
#include <unordered_map>
#include <memory>
#include "const.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "CSession.h"
#include "ChatGrpcClient.h"
#include "LogicWorker.h"
typedef function<void(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data)> FunCallBack;

class LogicSystem : public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();
	bool PostMsgToQueue(std::shared_ptr<LogicNode> msg);
	bool PostMsgToFileQue(std::shared_ptr <LogicNode> msg, int index);
	void SetServer(std::shared_ptr<CServer> pserver);
private:
	LogicSystem();
	void DealMsg();
	void RegisterCallBacks();
	void LoginChatCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data);
	void SearchUserCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data);
	void AddFriendCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data);
	void AuthFriendCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data);
	void TextChatMsgCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data);
	void TextChatAckCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data);
	void HeartbeatCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data);
	void DeliverPendingTextMessages(const std::shared_ptr<CSession>& session, int receiver_uid);
	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);
	bool isPureDigit(const std::string& str);
	void GetUserByName(std::string name, Json::Value& rtvalue);
	void GetUserByUid(std::string uid_str, Json::Value& rtvalue);
	bool GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& list);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list);
	std::queue<std::shared_ptr<LogicNode>> _msg_que;
	std::mutex _mutex;
	std::thread _worker_thread;
	std::condition_variable _consume;
	bool _b_stop;
	//将消息的id与回调函数相绑定
	std::map<short, FunCallBack> _func_callback;
	std::shared_ptr<CServer> _pre_server;
	std::vector<std::shared_ptr<LogicWorker> > _workers;
};
