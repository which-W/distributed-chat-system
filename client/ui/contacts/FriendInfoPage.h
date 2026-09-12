#pragma once
#include "usermgr.h"
#include <QWidget>
#include <QLabel>
#include <memory>
class FriendInfoPage : public QWidget {
    Q_OBJECT
public:
    explicit FriendInfoPage(QWidget* parent = nullptr);
    ~FriendInfoPage() override = default;
    void SetInfo(std::shared_ptr<UserInfo> user);
signals:
    void sig_jump_chat_item(std::shared_ptr<UserInfo> si);
private:
    void applyTheme();
    QLabel *avatar_, *name_, *uid_, *nick_, *remark_;
    std::shared_ptr<UserInfo> _user_info;
};
