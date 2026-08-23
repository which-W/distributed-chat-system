#pragma once
#include <QObject>
#include <memory>
#include "Singleton.h"
#include "UserData.h"
#include "global.h"
class UserMgr :public QObject, public Singleton<UserMgr>,
    public std::enable_shared_from_this<UserMgr>
{
    Q_OBJECT
public:
    friend class Singleton<UserMgr>;
    ~UserMgr();
    void SetToken(QString token);
    int GetUid();
    QString GetName();
    QString GetNick();
    QString GetIcon();
    QString GetDesc();
    bool IsLoadChatFin();
    void AppendApplyList(QJsonArray array);
    void AppendFriendList(QJsonArray array);
    void AddApplyList(std::shared_ptr<ApplyInfo> app);
	void SetUserInfo(std::shared_ptr<UserInfo> user_info);
    std::vector<std::shared_ptr<ApplyInfo>> GetApplyList();
	bool isAlreadyApply(int uid);
    bool CheckFriendById(int uid);
    void AddFriend(std::shared_ptr<AuthRsp> auth_rsp);
    void AddFriend(std::shared_ptr<AuthInfo> auth_info);
    std::shared_ptr<FriendInfo> GetFriendById(int uid);
    std::vector<std::shared_ptr<FriendInfo>> GetChatListPerPage();
    void UpdateChatLoadedCount();
    std::shared_ptr<UserInfo> GetUserInfo();
    std::vector<std::shared_ptr<FriendInfo>> GetConListPerPage();
    void AppendFriendChatMsg(int friend_id, std::vector<std::shared_ptr<TextChatData>>);
    void UpdateContactLoadedCount();
    bool IsLoadConFin();
    void ResetSession();
private:
    UserMgr();
    std::vector<std::shared_ptr<ApplyInfo>> _apply_list;
    std::vector<std::shared_ptr<FriendInfo>> _friend_list;
    QMap<int, std::shared_ptr<FriendInfo>> _friend_map;
    QString _token;
    int _chat_loaded;
    int _contact_loaded;
    std::shared_ptr<UserInfo> _user_info;
};
