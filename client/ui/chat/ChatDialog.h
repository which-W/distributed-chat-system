#pragma once

#include <QDialog>
#include "ui_ChatDialog.h"
#include <QAction>
#include "global.h"
#include "ChatUserWid.h"
#include <QRandomGenerator>
#include <QListWidgetItem>
#include "ClickedBtn.h"
#include "ChatUserList.h"
#include "LoadingWidget.h"
#include <QTimer>
#include "statewidget.h"
#include <QEvent>
#include <QMouseEvent>
#include "TcpMgr.h"
#include "ConUserItem.h"
QT_BEGIN_NAMESPACE
namespace Ui { class ChatDialogClass; };
QT_END_NAMESPACE

class ChatDialog : public QDialog
{
	Q_OBJECT

public:
	ChatDialog(QWidget * = nullptr);
	~ChatDialog();
	void ShowSearch(bool );
	void addChatUserList();
	void AddLBGroup(StateWidget * );
	void ClearLabelState(StateWidget * );
	void handleglobeQmousePress(QMouseEvent* );
	void SetSelectChatItem(int uid = 0);
	void SetSelectChatPage(int uid = 0);
	void LoadMoreChatWid();
	void LoadMoreConWid();
	void UpdateChatMsg(std::vector<std::shared_ptr<TextChatData> >);
protected:
	bool eventFilter(QObject* watch, QEvent* event) override;
private:
	Ui::ChatDialogClass *ui;
	ChatUIMode _mode;
	ChatUIMode _state;
	bool _b_loading;
	QListWidgetItem* _loadingitem;
	QList<StateWidget*> _lb_group;
	QWidget* _last_widget;
	QMap<int, QListWidgetItem*> _chat_items_added;
	int _cur_chat_uid;
public slots:
	void slot_loading_chat_user();
	void slot_side_chat();
	void slot_side_contact();
	void slot_apply_friend(std::shared_ptr<AddFriendApply> applyinfo);
	void slot_add_auth_friend(std::shared_ptr<AuthInfo> auth_info);
	void slot_auth_rsp(std::shared_ptr<AuthRsp> auth_rsp);
	void slot_jump_chat(std::shared_ptr<SearchInfo> user_info);
	void slot_loading_contact_user();
	void slot_switch_friend_info_page(std::shared_ptr<UserInfo> user_info);
	void slot_switch_apply_friend_page();
	void slot_jump_chat_again(std::shared_ptr<UserInfo>);
	void slot_append_send_chat_msg(std::shared_ptr<TextChatData>);
	void slot_item_clicked(QListWidgetItem* item);
	void slot_chat_msg_changed(std::shared_ptr<TextChatMsg>);
};
