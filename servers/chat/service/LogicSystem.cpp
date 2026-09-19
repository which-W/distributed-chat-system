#include "LogicSystem.h"
#include "ChatLogger.h"
#include "TextMessageService.h"
#include "ResourceClient.h"
#include "ResourceToken.h"
#include <charconv>

namespace {
std::size_t logicShardCount() {
    const char* value = std::getenv("CHAT_LOGIC_SHARDS");
    if (!value)
        return 4;
    std::size_t used = 0;
    const auto count = std::stoul(value, &used);
    if (used != std::string(value).size() || count < 1 || count > 64)
        throw std::invalid_argument("CHAT_LOGIC_SHARDS must be between 1 and 64");
    return count;
}
} // namespace

LogicSystem::LogicSystem() {
    RegisterCallBacks();

    // Attachment IO is owned exclusively by Resource Server.
    const auto count = logicShardCount();
    _delivery.resize(count);
    _delivery_cursor.resize(count);
    _notifications = std::make_unique<chat::runtime::KeyedExecutor>(2, 1024);
    _executor = std::make_unique<chat::runtime::KeyedExecutor>(
        count, MAX_MSG_QUEUE_SIZE,
        [this](std::size_t shard) {
            if (_ready.load())
                Tick(shard);
        },
        std::chrono::milliseconds(100));
    _ready.store(true);
}

std::size_t LogicSystem::SessionKey(const std::shared_ptr<CSession>& session) const {
    return std::hash<std::string>{}(session->GetSessionId());
}

void LogicSystem::DealMsg(const std::shared_ptr<LogicNode>& msg_node) {
    if (msg_node->_session->IsClosed())
        return;
    auto call_back_iter = _func_callback.find(msg_node->_recvnode->GetRecMsgNodeID());
    if (call_back_iter == _func_callback.end())
        return;
    if (msg_node->_recvnode->GetRecMsgNodeID() != MSG_CHAT_LOGIN &&
        msg_node->_session->GetUserId() <= 0) {
        msg_node->_session->Close();
        return;
    }
    try {
        call_back_iter->second(
            msg_node->_session, msg_node->_recvnode->GetRecMsgNodeID(),
            std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
    } catch (const std::exception& error) {
        chat::observability::log(chat::observability::Level::Warn, "chat.message_rejected",
                                 "malformed chat message rejected",
                                 {{"session_id", msg_node->_session->GetSessionId()},
                                  {"error", std::string(error.what())}});
        msg_node->_session->Close();
    } catch (...) {
        chat::observability::log(chat::observability::Level::Warn, "chat.message_rejected",
                                 "malformed chat message rejected",
                                 {{"session_id", msg_node->_session->GetSessionId()}});
        msg_node->_session->Close();
    }
}

bool LogicSystem::PostMsgToQueue(shared_ptr<LogicNode> msg) {
    const auto key = SessionKey(msg->_session);
    return _executor->post(key, [this, msg] { DealMsg(msg); });
}

bool LogicSystem::WakeDelivery(int uid) {
    auto session = UserMgr::GetInstance()->GetSession(uid);
    if (!session || session->IsClosed())
        return true;
    return _executor->post(SessionKey(session), [this, session, uid] {
        if (!session->IsClosed())
            DeliverPendingTextMessages(session, uid);
    });
}

void LogicSystem::Tick(std::size_t shard) {
    const auto now = std::chrono::steady_clock::now();
    auto& states = _delivery[shard];
    // Bound maintenance work so a large online population cannot monopolize
    // the shard. Rotate the cursor to avoid starving later sessions.
    const auto scan_budget = std::min<std::size_t>(states.size(), 128);
    unsigned queries = 0;
    for (std::size_t scanned = 0; scanned < scan_budget && !states.empty() && queries < 4;
         ++scanned) {
        auto it = states.upper_bound(_delivery_cursor[shard]);
        if (it == states.end())
            it = states.begin();
        _delivery_cursor[shard] = it->first;
        auto session = it->second.session.lock();
        if (!session || session->IsClosed() ||
            UserMgr::GetInstance()->GetSession(session->GetUserId()) != session) {
            states.erase(it);
            continue;
        }
        if (now >= it->second.next) {
            DeliverPendingTextMessages(session, session->GetUserId());
            ++queries;
        }
    }
    static thread_local auto last_metrics = now;
    if (now - last_metrics < std::chrono::seconds(10))
        return;
    last_metrics = now;
    std::uint64_t inflight = 0;
    for (const auto& item : states)
        inflight += item.second.window.size();
    chat::observability::log(chat::observability::Level::Info, "runtime.shard_metrics",
                             "current shard gauges",
                             {{"shard", std::uint64_t(shard)},
                              {"sessions", std::uint64_t(states.size())},
                              {"delivery_inflight", inflight}});
    if (shard != 0)
        return;
    auto metrics = _executor->metrics();
    auto notifications = _notifications->metrics();
    chat::observability::log(
        chat::observability::Level::Info, "runtime.metrics",
        "cumulative counters and current gauges",
        {{"queue_depth", metrics.queued},
         {"tasks_completed", metrics.completed},
         {"queue_rejected", metrics.rejected},
         {"task_failures", metrics.failures},
         {"queue_wait_us", metrics.wait_us},
         {"task_run_us", metrics.run_us},
         {"notify_queue_depth", notifications.queued},
         {"notify_rejected", notifications.rejected},
         {"notify_failures", notifications.failures},
         {"notify_cancelled", notifications.cancelled},
         {"delivery_attempts", _delivered.load()},
         {"delivery_retries", _retries.load()},
         {"ack_failures", _ack_failures.load()},
         {"pending_query_failures", _query_failures.load()},
         {"persist_us", _persist_us.load()},
         {"persist_batches", _persist_batches.load()},
         {"mysql_borrows", chat::observability::mysql_pool_metrics.borrows.load()},
         {"mysql_borrow_failures", chat::observability::mysql_pool_metrics.failed_borrows.load()},
         {"mysql_pool_wait_us", chat::observability::mysql_pool_metrics.wait_us.load()},
         {"redis_borrows", chat::observability::redis_pool_metrics.borrows.load()},
         {"redis_borrow_failures", chat::observability::redis_pool_metrics.failed_borrows.load()},
         {"redis_pool_wait_us", chat::observability::redis_pool_metrics.wait_us.load()},
         {"log_dropped", chat::observability::droppedCount()}});
}

bool LogicSystem::PostMsgToFileQue(shared_ptr<LogicNode> msg, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= _workers.size())
        return false;
    return _workers[index]->PostTask(msg);
}

void LogicSystem::SetServer(std::shared_ptr<CServer> pserver) {
    _pre_server = pserver;
}

bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo) {
    // 优先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        if (reader.parse(info_str, root) && root.isObject() && root["uid"].isInt() &&
            root["uid"].asInt() == uid && root["name"].isString() && root["email"].isString() &&
            root["nick"].isString() && root["desc"].isString() && root["sex"].isInt() &&
            root["icon"].isString()) {
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
        // redis中没有则查询mysql
        // 查询数据库
        std::shared_ptr<UserInfo> user_info = nullptr;
        user_info = MysqlMgr::GetInstance()->GetUser(uid);
        if (user_info == nullptr) {
            return false;
        }

        userinfo = user_info;

        // 将数据库内容写入redis缓存
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
    if (!reader.parse(msg_data, root) || !root.isObject() || !root["uid"].isInt() ||
        !root["token"].isString()) {
        session->Close();
        return;
    }
    auto uid = root["uid"].asInt();
    auto token = root["token"].asString();
    Json::Value rtvalue;
    bool authenticated = false;
    Defer defer([this, &rtvalue, session, uid, &authenticated]() {
        std::string return_str = rtvalue.toStyledString();
        if (session->Send(return_str, MSG_CHAT_LOGIN_RSP) && authenticated) {
            DeliverPendingTextMessages(session, uid);
        }
    });
    if (session->GetUserId() > 0 || uid <= 0 || token.empty()) {
        rtvalue["error"] = ErrorCodes::TokenInvalid;
        return;
    }
    if (root.get("resource_protocol_version", 0).asInt() != 1) {
        rtvalue["error"] = 426;
        rtvalue["message"] = "Please upgrade the client: HTTPS resource protocol v1 required";
        return;
    }
    std::string uid_str = std::to_string(uid);
    std::string ticket_value;
    const std::string ticket_key = CHAT_TICKET_PREFIX + token;
    if (!RedisMgr::GetInstance()->GetDel(ticket_key, ticket_value)) {
        chat::observability::log(
            chat::observability::Level::Warn, "auth.ticket_rejected",
            "chat ticket was missing or already consumed",
            {{"session_id", session->GetSessionId()}, {"uid", std::int64_t(uid)}});
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }
    Json::Value ticket;
    if (!reader.parse(ticket_value, ticket) || !ticket.isObject() || !ticket["uid"].isInt() ||
        !ticket["server"].isString() || ticket["uid"].asInt() != uid ||
        ticket["server"].asString() != ConfigMgr::Inst()["SelfServer"]["Name"]) {
        rtvalue["error"] = ErrorCodes::TokenInvalid;
        return;
    }

    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
    // 如果token匹配成功，则查询用户信息
    // 从redis中获取用户基本信息
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

    // 从数据库获取申请列表
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

    // 获取好友列表

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

    // 临时附件元数据随登录恢复，文件本体仍需接收方点击后按权限分片下载。
    rtvalue["pending_files"] = PendingResources(uid);
    rtvalue["resources_unavailable"] = rtvalue["pending_files"].isNull();

    auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");

    // session绑定用户uid
    session->SetUserId(uid);

    // 为用户设置登录ip server的名字
    std::string ipkey = USERIPPREFIX + uid_str;
    RedisMgr::GetInstance()->Set(ipkey, server_name);

    // uid和session绑定管理,方便以后踢人操作
    UserMgr::GetInstance()->SetUserSession(uid, session);
    authenticated = true;
    chat::observability::log(chat::observability::Level::Info, "auth.session_authenticated",
                             "chat session authenticated",
                             {{"session_id", session->GetSessionId()}, {"uid", std::int64_t(uid)}});

    return;
}

void LogicSystem::SearchUserCallback(std::shared_ptr<CSession> session, short msg_id,
                                     string msg_data) {
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(msg_data, root) || !root.isObject() || !root["uid"].isString()) {
        Json::Value invalid;
        invalid["error"] = ErrorCodes::JSON_ERROR;
        session->Send(invalid.toStyledString(), ID_SEARCH_USER_RSP);
        return;
    }
    auto uid_str = root["uid"].asString();
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
        // 如果是纯数字则查询uid
        GetUserByUid(uid_str, rtvalue);
    } else {
        // 否则查询用户名
        GetUserByName(uid_str, rtvalue);
    }
    return;
}

void LogicSystem::AddFriendCallback(std::shared_ptr<CSession> session, short msg_id,
                                    string msg_data) {
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(msg_data, root) || !root.isObject() || !root["touid"].isInt() ||
        (root.isMember("applyname") && !root["applyname"].isString()) ||
        (root.isMember("bakname") && !root["bakname"].isString())) {
        session->Close();
        return;
    }
    auto uid = session->GetUserId();
    auto applyname = root["applyname"].asString();
    auto bakname = root["bakname"].asString();
    auto touid = root["touid"].asInt();

    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
    rtvalue["touid"] = touid;
    if (root["request_id"].isString() && root["request_id"].asString().size() <= 64)
        rtvalue["request_id"] = root["request_id"];
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, ID_ADD_FRIEND_RSP);
    });
    if (touid <= 0 || touid == uid) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    const auto relation = MysqlMgr::GetInstance()->CheckFriendRelation(uid, touid);
    if (!relation.has_value()) { rtvalue["error"] = ErrorCodes::RPC_ERROR; return; }
    if (*relation) { rtvalue["error"] = ErrorCodes::UidInvalid; return; }
    if (!MysqlMgr::GetInstance()->GetUser(touid) || !MysqlMgr::GetInstance()->AddFriendApply(uid, touid)) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    // 查找redis通过用户id查找对应的server ip
    std::string uid_str = std::to_string(touid);
    std::string ipkey = USERIPPREFIX + uid_str;
    std::string ip_str = "";
    bool b_ip = RedisMgr::GetInstance()->Get(ipkey, ip_str);
    if (!b_ip)
        return;

    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];

    // 查询发出请求的用户id的详细信息
    std::string base_key = USER_BASE_INFO + std::to_string(uid);
    auto apply_info = std::make_shared<UserInfo>();
    bool b_info = GetBaseInfo(base_key, uid, apply_info);
    if (b_info) {
        applyname = apply_info->name;
    }

    // 如果在同一服务器则直接通知对方有申请消息
    if (ip_str == self_name) {
        auto session = UserMgr::GetInstance()->GetSession(touid);
        if (session) {
            // 在内存中则直接发送通知对方
            Json::Value notify;
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
            if (!session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ))
                rtvalue["notification_pending"] = true;
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

    // 发送通知
    const auto delivery = ChatGrpcClient::GetInstance()->NotifyAddFriend(ip_str, add_req);
    // The application is durable even if the best-effort live notification fails.
    if (delivery.error() != ErrorCodes::ERROR_CODE_OK)
        rtvalue["notification_pending"] = true;

    return;
}

void LogicSystem::AuthFriendCallback(std::shared_ptr<CSession> session, short msg_id,
                                     string msg_data) {
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(msg_data, root) || !root.isObject() || !root["touid"].isInt() ||
        !root["back"].isString()) {
        session->Close();
        return;
    }

    auto uid = session->GetUserId();
    auto touid = root["touid"].asInt();
    auto back_name = root["back"].asString();
    chat::observability::stream(chat::observability::Level::Info)
        << "from " << uid << " auth friend to " << touid << std::endl;

    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
    auto user_info = std::make_shared<UserInfo>();
    rtvalue["uid"] = touid;
    if (root["request_id"].isString() && root["request_id"].asString().size() <= 64)
        rtvalue["request_id"] = root["request_id"];

    std::string base_key = USER_BASE_INFO + std::to_string(touid);
    bool b_info = GetBaseInfo(base_key, touid, user_info);
    if (b_info) {
        rtvalue["name"] = user_info->name;
        rtvalue["nick"] = user_info->nick;
        rtvalue["icon"] = user_info->icon;
        rtvalue["sex"] = user_info->sex;
        rtvalue["uid"] = touid;
    } else {
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

    // 申请状态与双向好友记录必须在同一事务内提交，同时保留原有业务错误码。
    const auto relation = MysqlMgr::GetInstance()->CheckFriendRelation(uid, touid);
    if (!relation.has_value()) { rtvalue["error"] = ErrorCodes::RPC_ERROR; return; }
    const auto acceptance = *relation ? chat::storage::FriendAcceptanceResult::Success
                                      : MysqlMgr::GetInstance()->AddFriend(uid, touid, back_name);
    if (acceptance == chat::storage::FriendAcceptanceResult::PendingMissing) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }
    if (acceptance == chat::storage::FriendAcceptanceResult::StorageError) {
        rtvalue["error"] = ErrorCodes::RPC_ERROR;
        return;
    }

    // 查询redis 查找touid对应的server ip
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;
    std::string to_ip_value = "";
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
    if (!b_ip) {
        return;
    }

    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];
    // 直接通知对方有认证通过消息
    if (to_ip_value == self_name) {
        auto session = UserMgr::GetInstance()->GetSession(touid);
        if (session) {
            // 在内存中则直接发送通知对方
            Json::Value notify;
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
            } else {
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

    // 发送通知
    ChatGrpcClient::GetInstance()->NotifyAuthFriend(to_ip_value, auth_req);
}

void LogicSystem::TextChatMsgCallback(std::shared_ptr<CSession> session, short msg_id,
                                      string msg_data) {
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(msg_data, root) || !root.isObject() || !root["touid"].isInt() ||
        !root["text_array"].isArray() || root["text_array"].empty() ||
        root["text_array"].size() > MAX_TEXT_MESSAGES_PER_FRAME) {
        session->Close();
        return;
    }

    const int uid = session->GetUserId();
    const int touid = root["touid"].asInt();
    const Json::Value arrays = root["text_array"];
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
    rtvalue["text_array"] = arrays;
    rtvalue["fromuid"] = uid;
    rtvalue["touid"] = touid;
    Defer defer(
        [&rtvalue, session]() { session->Send(rtvalue.toStyledString(), ID_TEXT_CHAT_MSG_RSP); });

    std::vector<chat::messages::TextMessage> messages;
    for (const auto& text : arrays) {
        if (!text.isObject() || !text["content"].isString() || !text["msgid"].isString()) {
            rtvalue["error"] = ErrorCodes::JSON_ERROR;
            return;
        }
        chat::messages::TextMessage message;
        message.client_message_id = text["msgid"].asString();
        message.content = text["content"].asString();
        message.sender_uid = uid;
        message.receiver_uid = touid;
        messages.push_back(std::move(message));
    }

    const chat::messages::TextMessageService service(
        [](int sender, int receiver) {
            return MysqlMgr::GetInstance()->CheckFriendRelation(sender, receiver);
        },
        [this](const auto& batch) {
            const auto start = std::chrono::steady_clock::now();
            const bool persisted = MysqlMgr::GetInstance()->PersistTextMessages(batch);
            _persist_us += std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
            ++_persist_batches;
            return persisted;
        });
    using Acceptance = chat::messages::TextMessageService::Result;
    const auto result = service.accept(uid, touid, messages);
    if (result == Acceptance::InvalidMessage || result == Acceptance::InvalidRecipient) {
        rtvalue["error"] =
            result == Acceptance::InvalidMessage ? ErrorCodes::JSON_ERROR : ErrorCodes::UidInvalid;
        return;
    }
    if (result != Acceptance::Accepted) {
        chat::observability::log(chat::observability::Level::Error, "message.persist_failed",
                                 "text messages could not be persisted",
                                 {{"session_id", session->GetSessionId()},
                                  {"uid", std::int64_t(uid)},
                                  {"peer_uid", std::int64_t(touid)}});
        rtvalue["error"] = ErrorCodes::RPC_ERROR;
        rtvalue["retryable"] = true;
        return;
    }
    rtvalue["delivery"] = "accepted";
    chat::observability::log(chat::observability::Level::Info, "message.persisted",
                             "text messages persisted",
                             {{"session_id", session->GetSessionId()},
                              {"uid", std::int64_t(uid)},
                              {"peer_uid", std::int64_t(touid)},
                              {"message_count", std::uint64_t(messages.size())}});

    // Acceptance depends on the commit. A lost wakeup is covered by reconciliation.
    if (!_notifications->post(static_cast<std::size_t>(touid),
                              [this, touid, messages] { NotifyRecipient(touid, messages); }))
        chat::observability::log(chat::observability::Level::Warn, "message.wakeup_deferred",
                                 "notification queue full; periodic reconciliation will deliver");
}

void LogicSystem::NotifyRecipient(int touid,
                                  const std::vector<chat::messages::TextMessage>& messages) {
    if (UserMgr::GetInstance()->GetSession(touid)) {
        WakeDelivery(touid);
        return;
    }
    std::string target_server;
    if (!RedisMgr::GetInstance()->Get(USERIPPREFIX + std::to_string(touid), target_server))
        return;
    if (target_server == ConfigMgr::Inst()["SelfServer"]["Name"])
        return;
    TextChatMsgReq request;
    request.set_fromuid(messages.front().sender_uid);
    request.set_touid(touid);
    for (const auto& message : messages) {
        auto* text = request.add_textmsgs();
        text->set_msgid(message.client_message_id);
        text->set_msgcontent(message.content);
    }
    Json::Value ignored;
    const auto response =
        ChatGrpcClient::GetInstance()->NotifyTextChatMsg(target_server, request, ignored);
    if (response.error() != ErrorCodes::ERROR_CODE_OK)
        chat::observability::log(chat::observability::Level::Warn, "message.peer_delivery_failed",
                                 "peer wakeup failed; receiver reconciliation will retry",
                                 {{"peer_uid", std::int64_t(touid)}});
}

void LogicSystem::TextChatAckCallback(std::shared_ptr<CSession> session, short, string msg_data) {
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(msg_data, root) || !root.isObject() || !root["fromuid"].isInt() ||
        !root["msgids"].isArray() || root["msgids"].empty() ||
        root["msgids"].size() > MAX_TEXT_MESSAGES_PER_FRAME) {
        session->Close();
        return;
    }
    const int sender_uid = root["fromuid"].asInt();
    std::vector<std::string> ids;
    for (const auto& value : root["msgids"]) {
        if (!value.isString() || value.asString().empty() ||
            value.asString().size() > MAX_TEXT_MESSAGE_ID_BYTES) {
            session->Close();
            return;
        }
        ids.push_back(value.asString());
    }
    if (!MysqlMgr::GetInstance()->AcknowledgeTextMessages(session->GetUserId(), sender_uid, ids)) {
        ++_ack_failures;
        chat::observability::log(
            chat::observability::Level::Warn, "message.ack_failed",
            "ACK transaction failed; pending messages remain retryable",
            {{"session_id", session->GetSessionId()}, {"uid", std::int64_t(session->GetUserId())}});
        return;
    }
    chat::observability::log(chat::observability::Level::Info, "message.acknowledged",
                             "text messages acknowledged",
                             {{"session_id", session->GetSessionId()},
                              {"uid", std::int64_t(session->GetUserId())},
                              {"peer_uid", std::int64_t(sender_uid)},
                              {"message_count", std::uint64_t(ids.size())}});
    DeliverPendingTextMessages(session, session->GetUserId());
}

void LogicSystem::DeliverPendingTextMessages(const std::shared_ptr<CSession>& session,
                                             int receiver_uid) {
    if (session->IsClosed())
        return;
    auto& state = _delivery[_executor->shard(SessionKey(session))][session->GetSessionId()];
    state.session = session;
    const auto now = std::chrono::steady_clock::now();
    auto pending = MysqlMgr::GetInstance()->GetPendingTextMessages(
        receiver_uid, chat::messages::DeliveryWindow::Capacity);
    if (!pending) {
        ++_query_failures;
        state.idle_seconds = std::min(state.idle_seconds * 2, 30U);
        state.next = now + std::chrono::seconds(state.idle_seconds);
        return;
    }
    state.window.reconcile(*pending);
    state.idle_seconds = pending->empty() ? std::min(state.idle_seconds * 2, 30U) : 1;
    state.next = now + std::chrono::seconds(state.idle_seconds);
    for (const auto& message : *pending) {
        if (!state.window.due(message, now))
            continue;
        Json::Value payload;
        payload["error"] = ErrorCodes::ERROR_CODE_OK;
        payload["fromuid"] = message.sender_uid;
        payload["touid"] = message.receiver_uid;
        Json::Value text;
        text["msgid"] = message.client_message_id;
        text["content"] = message.content;
        payload["text_array"].append(text);
        if (!session->Send(payload.toStyledString(), ID_NOTIFY_TEXT_CHAT_MSG_REQ))
            break;
        if (state.window.sent(message, now))
            ++_retries;
        ++_delivered;
    }
}

bool LogicSystem::isPureDigit(const std::string& str) {
    if (str.empty())
        return false;
    for (char c : str) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

void LogicSystem::GetUserByUid(std::string uid_str, Json::Value& rtvalue) {
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;
    int uid = 0;
    const auto parsed = std::from_chars(uid_str.data(), uid_str.data() + uid_str.size(), uid);
    if (parsed.ec != std::errc{} || parsed.ptr != uid_str.data() + uid_str.size() || uid <= 0) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    std::string base_key = USER_BASE_INFO + uid_str;

    // 优先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        auto uid = root["uid"].asInt();
        auto name = root["name"].asString();
        auto nick = root["nick"].asString();
        auto desc = root["desc"].asString();
        auto sex = root["sex"].asInt();
        auto icon = root["icon"].asString();
        chat::observability::stream(chat::observability::Level::Info)
            << "user uid is " << uid << " name is " << name << endl;

        rtvalue["uid"] = uid;
        rtvalue["name"] = name;
        rtvalue["nick"] = nick;
        rtvalue["desc"] = desc;
        rtvalue["sex"] = sex;
        rtvalue["icon"] = icon;
        return;
    }

    // redis中没有则查询mysql
    // 查询数据库
    std::shared_ptr<UserInfo> user_info = nullptr;
    user_info = MysqlMgr::GetInstance()->GetUser(uid);
    if (user_info == nullptr) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    // 将数据库内容写入redis缓存
    Json::Value redis_root;
    redis_root["uid"] = user_info->uid;
    redis_root["name"] = user_info->name;
    redis_root["email"] = user_info->email;
    redis_root["nick"] = user_info->nick;
    redis_root["desc"] = user_info->desc;
    redis_root["sex"] = user_info->sex;
    redis_root["icon"] = user_info->icon;

    RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

    // 返回数据
    rtvalue["uid"] = user_info->uid;
    rtvalue["name"] = user_info->name;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["sex"] = user_info->sex;
    rtvalue["icon"] = user_info->icon;
}

bool LogicSystem::GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& list) {
    // 从mysql获取好友申请列表
    return MysqlMgr::GetInstance()->GetApplyList(to_uid, list, 0, 50);
}

bool LogicSystem::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list) {
    // 从mysql获取好友列表
    return MysqlMgr::GetInstance()->GetFriendList(self_id, user_list);
}

void LogicSystem::GetUserByName(std::string name, Json::Value& rtvalue) {
    rtvalue["error"] = ErrorCodes::ERROR_CODE_OK;

    std::string base_key = NAME_INFO + name;

    // 优先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        auto uid = root["uid"].asInt();
        auto name = root["name"].asString();
        auto nick = root["nick"].asString();
        auto desc = root["desc"].asString();
        auto sex = root["sex"].asInt();
        chat::observability::stream(chat::observability::Level::Info)
            << "user uid is " << uid << " name is " << name << endl;

        rtvalue["uid"] = uid;
        rtvalue["name"] = name;
        rtvalue["nick"] = nick;
        rtvalue["desc"] = desc;
        rtvalue["sex"] = sex;
        rtvalue["icon"] = root["icon"];
        if (!root.isMember("icon")) {
            const auto profile = MysqlMgr::GetInstance()->GetUser(uid);
            if (profile) rtvalue["icon"] = profile->icon;
        }
        return;
    }

    // redis中没有则查询mysql
    // 查询数据库
    std::shared_ptr<UserInfo> user_info = nullptr;
    user_info = MysqlMgr::GetInstance()->GetUser(name);
    if (user_info == nullptr) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    // 将数据库内容写入redis缓存
    Json::Value redis_root;
    redis_root["uid"] = user_info->uid;
    redis_root["name"] = user_info->name;
    redis_root["email"] = user_info->email;
    redis_root["nick"] = user_info->nick;
    redis_root["desc"] = user_info->desc;
    redis_root["sex"] = user_info->sex;
    redis_root["icon"] = user_info->icon;

    RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

    // 返回数据
    rtvalue["uid"] = user_info->uid;
    rtvalue["name"] = user_info->name;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["sex"] = user_info->sex;
    rtvalue["icon"] = user_info->icon;
}

void LogicSystem::RegisterCallBacks() {
    _func_callback[ID_RESOURCE_TOKEN_REQ] = [](std::shared_ptr<CSession> session, const short&, const std::string& body) {
        // 请求标识仅用于关联续领响应；身份始终取自已认证会话，不能由客户端指定。
        Json::Value request; Json::Reader reader;
        if (!reader.parse(body,request) || !request["request_id"].isString() ||
            request["request_id"].asString().empty() || request["request_id"].asString().size()>64)
            throw std::invalid_argument("resource credential request id required");
        Json::Value response; response["error"] = 0;
        response["uid"]=session->GetUserId(); response["request_id"]=request["request_id"];
        auto redis = RedisMgr::GetInstance();
        const auto token = chat::resources::newToken();
        Json::Value record; record["uid"] = session->GetUserId(); record["session"] = session->GetSessionId();
        record["generation"] = session->GetSessionId();
        const bool ok = redis->SetWithTtl(chat::resources::sessionKey(session->GetSessionId()), session->GetSessionId(), chat::resources::TokenSeconds) &&
            redis->SetWithTtl(chat::resources::tokenKey(token), record.toStyledString(), chat::resources::TokenSeconds);
        if (ok) { response["token"] = token; response["expires_in"] = chat::resources::TokenSeconds; }
        else response["error"] = 503;
        session->Send(response.toStyledString(), ID_RESOURCE_TOKEN_RSP);
    };
    _func_callback[ID_RESOURCE_REVOKE_REQ] = [](std::shared_ptr<CSession> session, const short&, const std::string&) {
        RedisMgr::GetInstance()->Del(chat::resources::sessionKey(session->GetSessionId()));
        session->Close();
    };
    _func_callback[MSG_CHAT_LOGIN] =
        std::bind(&LogicSystem::LoginChatCallback, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3);
    _func_callback[ID_SEARCH_USER_REQ] =
        std::bind(&LogicSystem::SearchUserCallback, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3);
    _func_callback[ID_ADD_FRIEND_REQ] =
        std::bind(&LogicSystem::AddFriendCallback, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3);
    _func_callback[ID_AUTH_FRIEND_REQ] =
        std::bind(&LogicSystem::AuthFriendCallback, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3);
    _func_callback[ID_TEXT_CHAT_MSG_REQ] =
        std::bind(&LogicSystem::TextChatMsgCallback, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3);
    _func_callback[ID_TEXT_CHAT_MSG_ACK] =
        std::bind(&LogicSystem::TextChatAckCallback, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3);
    _func_callback[ID_HEART_BEAT_REQ] =
        std::bind(&LogicSystem::HeartbeatCallback, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3);
}

void LogicSystem::HeartbeatCallback(std::shared_ptr<CSession> session, short, string msg_data) {
    session->TouchActivity();
    Json::Value request;
    Json::Reader reader;
    if (!reader.parse(msg_data, request) || !request.isObject() || !request["sync_friends"].isBool() ||
        !request["sync_friends"].asBool()) {
        session->Send("{}", ID_HEARTBEAT_RSP);
        return;
    }
    // Durable recovery also covers missed cross-server notifications. Send small
    // batches so a large address book cannot overflow the 16-bit frame length.
    Json::Value snapshot;
    snapshot["apply_list"] = Json::Value(Json::arrayValue);
    snapshot["friend_list"] = Json::Value(Json::arrayValue);
    snapshot["pending_files"] = Json::Value(Json::arrayValue);
    auto flush = [&]() {
        session->Send(snapshot.toStyledString(), ID_HEARTBEAT_RSP);
        snapshot["apply_list"].clear();
        snapshot["friend_list"].clear();
        snapshot["pending_files"].clear();
    };
    std::vector<std::shared_ptr<ApplyInfo>> applications;
    if (GetFriendApplyInfo(session->GetUserId(), applications)) {
        for (const auto& apply : applications) {
            Json::Value item;
            item["uid"] = apply->_uid;
            item["name"] = apply->_name;
            item["nick"] = apply->_nick;
            item["icon"] = apply->_icon;
            item["sex"] = apply->_sex;
            item["desc"] = apply->_desc;
            item["status"] = apply->_status;
            if (snapshot.toStyledString().size() + item.toStyledString().size() > 12 * 1024) flush();
            snapshot["apply_list"].append(item);
            if (snapshot["apply_list"].size() == 20) flush();
        }
    }
    std::vector<std::shared_ptr<UserInfo>> friends;
    if (GetFriendList(session->GetUserId(), friends)) {
        for (const auto& peer : friends) {
            Json::Value item;
            item["uid"] = peer->uid;
            item["name"] = peer->name;
            item["nick"] = peer->nick;
            item["icon"] = peer->icon;
            item["sex"] = peer->sex;
            item["desc"] = peer->desc;
            item["back"] = peer->back;
            if (snapshot.toStyledString().size() + item.toStyledString().size() > 12 * 1024) flush();
            snapshot["friend_list"].append(item);
            if (snapshot["friend_list"].size() == 20) flush();
        }
    }
    const auto files = PendingResources(session->GetUserId());
    snapshot["resources_unavailable"] = files.isNull();
    for (const auto& item : files) {
        if (snapshot.toStyledString().size() + item.toStyledString().size() > 12 * 1024) flush();
        snapshot["pending_files"].append(item);
        if (snapshot["pending_files"].size() == 20) flush();
    }
    flush();
}

LogicSystem::~LogicSystem() {
    Shutdown();
}

void LogicSystem::Shutdown() {
    if (_shutdown.exchange(true))
        return;
    // Notifications only wake delivery; durable pending messages remain in MySQL.
    _notifications->stop(false);
    _executor->stop();
}
