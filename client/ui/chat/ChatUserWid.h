#pragma once

#include <QWidget>
#include "ui_ChatUserWid.h"
#include "ListItemBase.h"
#include "usermgr.h"
#include "UserData.h"
QT_BEGIN_NAMESPACE
namespace Ui { class ChatUserWidClass; };
QT_END_NAMESPACE

class ChatUserWid : public ListItemBase
{
	Q_OBJECT

public:
    explicit ChatUserWid(QWidget* parent = nullptr);
    ~ChatUserWid();

    QSize sizeHint() const override {
        return QSize(250, 70); // 返回自定义的尺寸
    }

    void SetInfo(std::shared_ptr<FriendInfo> friend_info);
    void SetInfo(std::shared_ptr<UserInfo> user_info);
    std::shared_ptr<UserInfo> GetUserInfo();
    void updateLastMsg(std::vector<std::shared_ptr<TextChatData>> msgs);
private:
    Ui::ChatUserWidClass* ui;
	std::shared_ptr<UserInfo> _user_info;
};
