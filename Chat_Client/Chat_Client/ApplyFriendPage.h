#pragma once

#include <QWidget>
#include "ui_ApplyFriendPage.h"
#include "ApplyFriendItem.h"
#include "ApplyFriendList.h"
#include <unordered_map>
#include "TcpMgr.h"
#include <QRandomGenerator>
#include "usermgr.h"
#include "UserData.h"
#include <QPainter>
#include "AuthenFriend.h"
QT_BEGIN_NAMESPACE
namespace Ui { class ApplyFriendPageClass; };
QT_END_NAMESPACE

class ApplyFriendPage : public QWidget
{
	Q_OBJECT

public:
    explicit ApplyFriendPage(QWidget* parent = nullptr);
    ~ApplyFriendPage();
    void AddNewApply(std::shared_ptr<AddFriendApply> apply);
protected:
    void paintEvent(QPaintEvent* event);
private:
    void loadApplyList();
    Ui::ApplyFriendPageClass* ui;
    std::unordered_map<int, ApplyFriendItem*> _unauth_items;
public slots:
    void slot_auth_rsp(std::shared_ptr<AuthRsp>);
signals:
    void sig_show_search(bool);
};
