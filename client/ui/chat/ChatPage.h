#pragma once

#include <QWidget>
#include "ui_ChatPage.h"
#include <QStyleOption>
#include <QPainter>
#include "MessageTextEdit.h"
#include "ChatItemBase.h"
#include "global.h"
#include "PictureBubble.h"
#include "TextBuble.h"
#include <QTextEdit>
#include <QPixmap>
#include "usermgr.h"
#include "TcpMgr.h"
#include <QUuid>
#include "FileBubble.h"
#include "FileTransferManager.h"
#include <QHash>
QT_BEGIN_NAMESPACE
namespace Ui { class ChatPageClass; };
QT_END_NAMESPACE

class ChatPage : public QWidget
{
	Q_OBJECT

public:
	ChatPage(QWidget *parent = nullptr);
	~ChatPage();
	void paintEvent(QPaintEvent* event) override;
	void SetUserInfo(std::shared_ptr<UserInfo>);
	void AppendChatMsg(std::shared_ptr<TextChatData> msg);
private slots:
	void sendMessage();
	void on_send_btn_clicked();
	void chooseFile(QString, ClickLbState);
signals:
	void sig_append_send_chat_msg(std::shared_ptr<TextChatData>);
private:
	Ui::ChatPageClass *ui;
	std::shared_ptr<UserInfo> _user_info;
	QHash<QString, FileBubble*> _file_bubbles;
	void appendFileBubble(const QJsonObject& metadata, ChatRole role, bool incoming);
};
