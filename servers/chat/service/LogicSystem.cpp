#include "LogicSystem.h"

LogicSystem::LogicSystem() :_b_stop(false) {
	RegisterCallBacks();
	_worker_thread = std::thread(&LogicSystem::DealMsg, this);

    for (int i = 0; i < LOGIC_WORKER_COUNT; i++) {
        _workers.push_back(std::make_shared<LogicWorker>());
    }
}

void LogicSystem::DealMsg() {
    for (;;) {
        std::unique_lock<std::mutex> unique_lk(_mutex);
        //判断队列为空则用条件变量阻塞等待，并释放锁
        while (_msg_que.empty() && !_b_stop) {
            _consume.wait(unique_lk);
        }
        //判断是否为关闭状态，把所有逻辑执行完后则退出循环
        if (_b_stop) {
            while (!_msg_que.empty()) {
                auto msg_node = _msg_que.front();
                cout << "recv_msg id  is " << msg_node->_recvnode->GetRecMsgNodeID() << endl;
	    auto call_back_iter = _func_callback.find(msg_node->_recvnode->GetRecMsgNodeID());
                if (call_back_iter == _func_callback.end()) {
                    _msg_que.pop();
			continue;
		}
		if (msg_node->_recvnode->GetRecMsgNodeID() != MSG_CHAT_LOGIN
			&& msg_node->_session->GetUserId() <= 0) {
			msg_node->_session->Close();
			_msg_que.pop();
			continue;
		}
                call_back_iter->second(msg_node->_session, msg_node->_recvnode->GetRecMsgNodeID(),
                    std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
                _msg_que.pop();
            }
            break;
        }
        //如果没有停服，且说明队列中有数据
        auto msg_node = _msg_que.front();
        auto call_back_iter = _func_callback.find(msg_node->_recvnode->GetRecMsgNodeID());
        if (call_back_iter == _func_callback.end()) {
            _msg_que.pop();
            continue;
        }
		if (msg_node->_recvnode->GetRecMsgNodeID() != MSG_CHAT_LOGIN
			&& msg_node->_session->GetUserId() <= 0) {
			msg_node->_session->Close();
			_msg_que.pop();
			continue;
		}
		std::cout << "recv_msg id  is " << msg_node->_recvnode->GetRecMsgNodeID() << std::endl;
		std::cout << "recv_msg data is " << std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len) << std::endl;
		//调用HandHead和HandleMsg的回调函数
        call_back_iter->second(msg_node->_session, msg_node->_recvnode->GetRecMsgNodeID(),
            std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
        _msg_que.pop();
     }
}

void LogicSystem::PostMsgToQueue(shared_ptr<LogicNode> msg) {
    std::unique_lock<std::mutex> unique_lk(_mutex);
    _msg_que.push(msg);
    //由0变为1则发送通知信号
    if (_msg_que.size() == 1) {
        unique_lk.unlock();
        _consume.notify_one();
    }
    //超过队列最大值通知并return
	if (_msg_que.size() > MAX_MSG_QUEUE_SIZE) {
		std::cout << "LogicSystem msg queue size is full, size is " << MAX_MSG_QUEUE_SIZE << std::endl;
		return;
	}
}

void LogicSystem::PostMsgToFileQue(shared_ptr<LogicNode>msg, int index)
{
    _workers[index]->PostTask(msg);
}

void LogicSystem::SetServer(std::shared_ptr<CServer> pserver)
{
    _pre_server = pserver;
}

bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
    //优先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        userinfo->uid = root["uid"].asInt();
        userinfo->name = root["name"].asString();
        userinfo->email = root["email"].asString();
        userinfo->nick = root["nick"].asString();
        userinfo->desc = root["desc"].asString();
        userinfo->sex = root["sex"].asInt();
        userinfo->icon = root["icon"].asString();
        std::cout << "user login uid is " << userinfo->uid << " name is " << userinfo->name << endl;
    }
    else {
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

void LogicSystem::LoginChatCallback(shared_ptr<CSession> session, short msg_id, string msg_data) {
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);
    std::cout << "user login uid is " << root["uid"].asInt() << endl;
	auto uid = root["uid"].asInt();
    auto token = root["token"].asString();
    Json::Value rtvalue;
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, MSG_CHAT_LOGIN_RSP);
        });
	if (session->GetUserId() > 0 || uid <= 0 || token.empty()) {
		rtvalue["error"] = ErrorCodes::TokenInvalid;
		return;
	}
	std::string uid_str = std::to_string(uid);
	std::string ticket_value;
	const std::string ticket_key = CHAT_TICKET_PREFIX + token;
	if (!RedisMgr::GetInstance()->GetDel(ticket_key, ticket_value)) {
			rtvalue["error"] = ErrorCodes::UidInvalid;
			return;
	    }
	Json::Value ticket;
	if (!reader.parse(ticket_value, ticket)
		|| ticket["uid"].asInt() != uid
		|| ticket["server"].asString() != ConfigMgr::Inst()["SelfServer"]["Name"]) {
			rtvalue["error"] = ErrorCodes::TokenInvalid;
			return;
	    }

    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
	//如果token匹配成功，则查询用户信息
	//从redis中获取用户基本信息
    std::string base_key = USER_BASE_INFO + uid_str;
    auto user_info = std::make_shared<UserInfo>();
    bool b_base = GetBaseInfo(base_key, uid, user_info);
    if (!b_base) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }
    rtvalue["uid"] = uid;
    rtvalue["name"] = user_info->name;
    rtvalue["email"] = user_info->email;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["sex"] = user_info->sex;
    rtvalue["icon"] = user_info->icon;

    //从数据库获取申请列表
    std::vector<std::shared_ptr<ApplyInfo>> apply_list;
    auto b_apply = GetFriendApplyInfo(uid, apply_list);
    if (b_apply) {
        for (auto& apply : apply_list) {
            Json::Value obj;
            obj["name"] = apply->_name;
            obj["uid"] = apply->_uid;
            obj["icon"] = apply->_icon;
            obj["nick"] = apply->_nick;
            obj["sex"] = apply->_sex;
            obj["desc"] = apply->_desc;
            obj["status"] = apply->_status;
            rtvalue["apply_list"].append(obj);
        }
    }

    //获取好友列表

    std::vector<std::shared_ptr<UserInfo>> friend_list;
    bool b_friend_list = GetFriendList(uid, friend_list);
    for (auto& friend_ele : friend_list) {
        Json::Value obj;
        obj["name"] = friend_ele->name;
        obj["uid"] = friend_ele->uid;
        obj["icon"] = friend_ele->icon;
        obj["nick"] = friend_ele->nick;
        obj["sex"] = friend_ele->sex;
        obj["desc"] = friend_ele->desc;
        obj["back"] = friend_ele->back;
        rtvalue["friend_list"].append(obj);
    }


    auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");

    //session绑定用户uid
    session->SetUserId(uid);

    //为用户设置登录ip server的名字
    std::string  ipkey = USERIPPREFIX + uid_str;
    RedisMgr::GetInstance()->Set(ipkey, server_name);

    //uid和session绑定管理,方便以后踢人操作
    UserMgr::GetInstance()->SetUserSession(uid, session);

    return;
}

void LogicSystem::SearchUserCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data)
{
    Json::Reader reader;
    Json::Value root;
	reader.parse(msg_data, root);
    auto uid_str = root["uid"].asString();
    std::cout << "user SearchInfo uid is  " << uid_str << endl;
	Json::Value rtvalue;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_SEARCH_USER_RSP);
		});

	if (uid_str.empty()) {
		rtvalue["error"] = ErrorCodes::JSON_ERROR;
		return;
	}

	if (isPureDigit(uid_str)) {
		//如果是纯数字则查询uid
		GetUserByUid(uid_str, rtvalue);
	}
	else {
		//否则查询用户名
		GetUserByName(uid_str, rtvalue);
	}
    return;
}

void LogicSystem::AddFriendCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data)
{
	Json::Reader reader;
    Json::Value root;
	reader.parse(msg_data, root);
	    auto uid = session->GetUserId();
	auto applyname = root["applyname"].asString();
    auto bakname = root["bakname"].asString();
    auto touid = root["touid"].asInt();

	Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_ADD_FRIEND_RSP);
		});
	if (touid <= 0 || touid == uid) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

    if (!MysqlMgr::GetInstance()->AddFriendApply(uid, touid)) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

    //查找redis通过用户id查找对应的server ip
	std::string uid_str = std::to_string(touid);
	std::string ipkey = USERIPPREFIX + uid_str;
	std::string ip_str = "";
	bool b_ip = RedisMgr::GetInstance()->Get(ipkey , ip_str);
    if (!b_ip) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];

    //查询发出请求的用户id的详细信息
    std::string base_key = USER_BASE_INFO + std::to_string(uid);
    auto apply_info = std::make_shared<UserInfo>();
    bool b_info = GetBaseInfo(base_key, uid, apply_info);
	if (b_info) {
		applyname = apply_info->name;
	}

    //如果在同一服务器则直接通知对方有申请消息
    if (ip_str == self_name) {
        auto session = UserMgr::GetInstance()->GetSession(touid);
        if (session) {
            //在内存中则直接发送通知对方
            Json::Value  notify;
            notify["error"] = ErrorCodes::ERROR_CODE_OK;
            notify["applyuid"] = uid;
            notify["name"] = applyname;
            notify["desc"] = "";
            if (b_info) {
                notify["icon"] = apply_info->icon;
                notify["sex"] = apply_info->sex;
                notify["nick"] = apply_info->nick;
            }
            std::string return_str = notify.toStyledString();
            session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
        }

        return;
    }

    AddFriendReq add_req;
    add_req.set_applyuid(uid);
    add_req.set_touid(touid);
    add_req.set_name(applyname);
    add_req.set_desc("");
    if (b_info) {
        add_req.set_icon(apply_info->icon);
        add_req.set_sex(apply_info->sex);
        add_req.set_nick(apply_info->nick);
    }

    //发送通知
    ChatGrpcClient::GetInstance()->NotifyAddFriend(ip_str, add_req);


    return;

}

void LogicSystem::AuthFriendCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);

	    auto uid = session->GetUserId();
    auto touid = root["touid"].asInt();
    auto back_name = root["back"].asString();
    std::cout << "from " << uid << " auth friend to " << touid << std::endl;

    Json::Value  rtvalue;
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
    auto user_info = std::make_shared<UserInfo>();

    std::string base_key = USER_BASE_INFO + std::to_string(touid);
    bool b_info = GetBaseInfo(base_key, touid, user_info);
    if (b_info) {
        rtvalue["name"] = user_info->name;
        rtvalue["nick"] = user_info->nick;
        rtvalue["icon"] = user_info->icon;
        rtvalue["sex"] = user_info->sex;
        rtvalue["uid"] = touid;
    }
    else {
        rtvalue["error"] = ErrorCodes::UidInvalid;
    }


    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, ID_AUTH_FRIEND_RSP);
        });
	if (touid <= 0 || touid == uid || !b_info) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

    //先更新数据库
    if (!MysqlMgr::GetInstance()->AuthFriendApply(uid, touid)) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

    //更新数据库添加好友
    if (!MysqlMgr::GetInstance()->AddFriend(uid, touid, back_name)) {
		rtvalue["error"] = ErrorCodes::RPC_ERROR;
		return;
	}

    //查询redis 查找touid对应的server ip
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;
    std::string to_ip_value = "";
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
    if (!b_ip) {
        return;
    }

    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];
    //直接通知对方有认证通过消息
    if (to_ip_value == self_name) {
        auto session = UserMgr::GetInstance()->GetSession(touid);
        if (session) {
            //在内存中则直接发送通知对方
            Json::Value  notify;
            notify["error"] = ErrorCodes::ERROR_CODE_OK;
            notify["fromuid"] = uid;
            notify["touid"] = touid;
            std::string base_key = USER_BASE_INFO + std::to_string(uid);
            auto user_info = std::make_shared<UserInfo>();
            bool b_info = GetBaseInfo(base_key, uid, user_info);
            if (b_info) {
                notify["name"] = user_info->name;
                notify["nick"] = user_info->nick;
                notify["icon"] = user_info->icon;
                notify["sex"] = user_info->sex;
            }
            else {
                notify["error"] = ErrorCodes::UidInvalid;
            }


            std::string return_str = notify.toStyledString();
            session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
        }

        return;
    }


    AuthFriendReq auth_req;
    auth_req.set_fromuid(uid);
    auth_req.set_touid(touid);

    //发送通知
    ChatGrpcClient::GetInstance()->NotifyAuthFriend(to_ip_value, auth_req);


}

void LogicSystem::TextChatMsgCallback(std::shared_ptr<CSession> session, short msg_id, string msg_data)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);

	    auto uid = session->GetUserId();
    auto touid = root["touid"].asInt();

    const Json::Value  arrays = root["text_array"];

    Json::Value  rtvalue;
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
    rtvalue["text_array"] = arrays;
    rtvalue["fromuid"] = uid;
    rtvalue["touid"] = touid;

    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);
        });
	if (touid <= 0 || touid == uid) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}


    //查询redis 查找touid对应的server ip
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;
    std::string to_ip_value = "";
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
    if (!b_ip) {
        return;
    }

    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];
    //直接通知对方有认证通过消息
    if (to_ip_value == self_name) {
        auto session = UserMgr::GetInstance()->GetSession(touid);
        if (session) {
            //在内存中则直接发送通知对方
            std::string return_str = rtvalue.toStyledString();
            session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
        }

        return;
    }


    TextChatMsgReq text_msg_req;
    text_msg_req.set_fromuid(uid);
    text_msg_req.set_touid(touid);
    for (const auto& txt_obj : arrays) {
        auto content = txt_obj["content"].asString();
        auto msgid = txt_obj["msgid"].asString();
        std::cout << "content is " << content << std::endl;
        std::cout << "msgid is " << msgid << std::endl;
        auto* text_msg = text_msg_req.add_textmsgs();
        text_msg->set_msgid(msgid);
        text_msg->set_msgcontent(content);
    }


    //发送通知
    ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req, rtvalue);
}

bool LogicSystem::isPureDigit(const std::string& str)
{
    for (char c : str) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

void LogicSystem::GetUserByUid(std::string uid_str, Json::Value& rtvalue)
{
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;

    std::string base_key = USER_BASE_INFO + uid_str;

    //优先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        auto uid = root["uid"].asInt();
        auto name = root["name"].asString();
        auto email = root["email"].asString();
        auto nick = root["nick"].asString();
        auto desc = root["desc"].asString();
        auto sex = root["sex"].asInt();
        auto icon = root["icon"].asString();
        std::cout << "user uid is " << uid << " name is " << name << endl;

        rtvalue["uid"] = uid;
        rtvalue["name"] = name;
        rtvalue["email"] = email;
        rtvalue["nick"] = nick;
        rtvalue["desc"] = desc;
        rtvalue["sex"] = sex;
        rtvalue["icon"] = icon;
        return;
    }

    auto uid = std::stoi(uid_str);
    //redis中没有则查询mysql
    //查询数据库
    std::shared_ptr<UserInfo> user_info = nullptr;
    user_info = MysqlMgr::GetInstance()->GetUser(uid);
    if (user_info == nullptr) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    //将数据库内容写入redis缓存
    Json::Value redis_root;
    redis_root["uid"] = user_info->uid;
    redis_root["name"] = user_info->name;
    redis_root["email"] = user_info->email;
    redis_root["nick"] = user_info->nick;
    redis_root["desc"] = user_info->desc;
    redis_root["sex"] = user_info->sex;
    redis_root["icon"] = user_info->icon;

    RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

    //返回数据
    rtvalue["uid"] = user_info->uid;
    rtvalue["name"] = user_info->name;
    rtvalue["email"] = user_info->email;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["sex"] = user_info->sex;
    rtvalue["icon"] = user_info->icon;
}

bool LogicSystem::GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& list) {
    //从mysql获取好友申请列表
    return MysqlMgr::GetInstance()->GetApplyList(to_uid, list, 0, 10);
}

bool LogicSystem::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list) {
    //从mysql获取好友列表
    return MysqlMgr::GetInstance()->GetFriendList(self_id, user_list);
}

void LogicSystem::GetUserByName(std::string name, Json::Value& rtvalue)
{
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;

    std::string base_key = NAME_INFO + name;

    //优先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        auto uid = root["uid"].asInt();
        auto name = root["name"].asString();
        auto email = root["email"].asString();
        auto nick = root["nick"].asString();
        auto desc = root["desc"].asString();
        auto sex = root["sex"].asInt();
        std::cout << "user uid is " << uid << " name is " << name << endl;

        rtvalue["uid"] = uid;
        rtvalue["name"] = name;
        rtvalue["email"] = email;
        rtvalue["nick"] = nick;
        rtvalue["desc"] = desc;
        rtvalue["sex"] = sex;
        return;
    }

    //redis中没有则查询mysql
    //查询数据库
    std::shared_ptr<UserInfo> user_info = nullptr;
    user_info = MysqlMgr::GetInstance()->GetUser(name);
    if (user_info == nullptr) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    //将数据库内容写入redis缓存
    Json::Value redis_root;
    redis_root["uid"] = user_info->uid;
    redis_root["name"] = user_info->name;
    redis_root["email"] = user_info->email;
    redis_root["nick"] = user_info->nick;
    redis_root["desc"] = user_info->desc;
    redis_root["sex"] = user_info->sex;

    RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

    //返回数据
    rtvalue["uid"] = user_info->uid;
    rtvalue["name"] = user_info->name;
    rtvalue["email"] = user_info->email;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["sex"] = user_info->sex;
}

void LogicSystem::RegisterCallBacks() {
	_func_callback[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginChatCallback,this
		, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	_func_callback[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchUserCallback, this
        , std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	_func_callback[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendCallback, this
		, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	_func_callback[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::AuthFriendCallback, this
		, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	_func_callback[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::TextChatMsgCallback, this
		, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	_func_callback[ID_HEART_BEAT_REQ] = std::bind(&LogicSystem::HeartbeatCallback, this
		, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

void LogicSystem::HeartbeatCallback(std::shared_ptr<CSession> session, short, string msg_data) {
	if (msg_data.empty()) {
		msg_data = "{}";
	}
	session->Send(msg_data, ID_HEARTBEAT_RSP);
}

LogicSystem::~LogicSystem() {
    _b_stop = true;
    _consume.notify_one();
    _worker_thread.join();
}
