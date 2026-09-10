#pragma once
#include "CSession.h"
#include "ChatGrpcClient.h"
#include "DeliveryWindow.h"
#include "KeyedExecutor.h"
#include "LogicWorker.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "Singleton.h"
#include "StatusGrpcClient.h"
#include "UserMgr.h"
#include "const.h"
#include <functional>
#include <iostream>
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <map>
#include <memory>
#include <queue>
#include <thread>
#include <unordered_map>
typedef function<void(std::shared_ptr<CSession> session, const short& msg_id,
                      const std::string& msg_data)>
    FunCallBack;

class LogicSystem : public Singleton<LogicSystem> {
    friend class Singleton<LogicSystem>;

  public:
    ~LogicSystem();
    bool PostMsgToQueue(std::shared_ptr<LogicNode> msg);
    bool PostMsgToFileQue(std::shared_ptr<LogicNode> msg, int index);
    bool WakeDelivery(int receiver_uid);
    void Shutdown();
    void SetServer(std::shared_ptr<CServer> pserver);

  private:
    LogicSystem();
    void DealMsg(const std::shared_ptr<LogicNode>& message);
    void Tick(std::size_t shard);
    std::size_t SessionKey(const std::shared_ptr<CSession>& session) const;
    void NotifyRecipient(int receiver_uid,
                         const std::vector<chat::messages::TextMessage>& messages);
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
    struct DeliveryState {
        std::weak_ptr<CSession> session;
        chat::messages::DeliveryWindow window;
        std::chrono::steady_clock::time_point next{};
        unsigned idle_seconds = 1;
    };
    std::vector<std::map<std::string, DeliveryState>> _delivery;
    std::vector<std::string> _delivery_cursor;
    std::unique_ptr<chat::runtime::KeyedExecutor> _executor;
    std::unique_ptr<chat::runtime::KeyedExecutor> _notifications;
    std::atomic<std::uint64_t> _delivered{0}, _retries{0}, _ack_failures{0}, _query_failures{0};
    std::atomic<std::uint64_t> _persist_us{0}, _persist_batches{0};
    std::atomic<bool> _ready{false}, _shutdown{false};
    // 将消息的id与回调函数相绑定
    std::map<short, FunCallBack> _func_callback;
    std::shared_ptr<CServer> _pre_server;
    std::vector<std::shared_ptr<LogicWorker>> _workers;
};
