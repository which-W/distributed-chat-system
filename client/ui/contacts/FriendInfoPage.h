#pragma once

#include <QWidget>
#include "ui_FriendInfoPage.h"
#include <memory>
#include "usermgr.h"

QT_BEGIN_NAMESPACE
namespace Ui { class FriendInfoPage; };
QT_END_NAMESPACE

class FriendInfoPage : public QWidget
{
	Q_OBJECT

public:
    explicit FriendInfoPage(QWidget* parent = nullptr);
    ~FriendInfoPage();
    void SetInfo(std::shared_ptr<UserInfo> ui);
private slots:
    void on_msg_chat_clicked();

private:
    Ui::FriendInfoPage* ui;
    std::shared_ptr<UserInfo> _user_info;
signals:
    void sig_jump_chat_item(std::shared_ptr<UserInfo> si);
};
