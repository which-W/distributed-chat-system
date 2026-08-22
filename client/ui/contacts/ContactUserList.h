#pragma once
#include<QWidget>
#include <memory>
#include "global.h"
#include "UserData.h"
#include "ConUserItem.h"
#include <QListWidgetItem>
#include <QListWidget>
#include "TcpMgr.h"
#include <QWheelEvent>
#include <QTimer>
#include <QScrollBar>
#include "GroupTipItem.h"
#include <QRandomGenerator>
#include "statewidget.h"
#include "ConUserItem.h"
class ContactUserList:public QListWidget
{
	Q_OBJECT
public:
    ContactUserList(QWidget* parent = nullptr);
    void ShowRedPoint(bool bshow = true);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void addContactUserList();

public slots:
    void slot_item_clicked(QListWidgetItem* item);
    void slot_add_auth_firend(std::shared_ptr<AuthInfo>);
    void slot_auth_rsp(std::shared_ptr<AuthRsp>);
signals:
    void sig_loading_contact_user();
    void sig_switch_apply_friend_page();
    void sig_switch_friend_info_page(std::shared_ptr<UserInfo> user_info);
    void sig_clicked_user();
private:
    bool _load_pending;
    ConUserItem* _add_friend_item;
    QListWidgetItem* _groupitem;

};
