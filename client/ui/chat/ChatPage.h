#pragma once

#include "ChatItemBase.h"
#include "FileBubble.h"
#include "FileTransferManager.h"
#include "MessageTextEdit.h"
#include "PictureBubble.h"
#include "TcpMgr.h"
#include "TextBuble.h"
#include "global.h"
#include "ui_ChatPage.h"
#include "usermgr.h"
#include <QHash>
#include <QPointer>
#include <QPainter>
#include <QPixmap>
#include <QStyleOption>
#include <QTextEdit>
#include <QUuid>
#include <QWidget>
QT_BEGIN_NAMESPACE
namespace Ui {
class ChatPageClass;
};
QT_END_NAMESPACE

class ChatPage : public QWidget {
    Q_OBJECT

  public:
    ChatPage(QWidget* parent = nullptr);
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
    Ui::ChatPageClass* ui;
    std::shared_ptr<UserInfo> _user_info;
    // QPointer 在历史气泡或整个页面析构时自动置空，避免悬空控件指针。
    QHash<QString, QPointer<FileBubble>> _file_bubbles;
    void appendFileBubble(const QJsonObject& metadata, ChatRole role, bool incoming);
};
