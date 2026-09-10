#include "ChatPage.h"
#include <QFileDialog>
#include <QFileInfo>

ChatPage::ChatPage(QWidget* parent) : QWidget(parent), ui(new Ui::ChatPageClass()) {
    ui->setupUi(this);
    // 设置按钮样式
    ui->send_btn->SetState("normal", "hover", "press");

    // 设置图标样式
    ui->emo_lb->SetState("normal", "hover", "press", "normal", "hover", "press");
    ui->file_lb->SetState("normal", "hover", "press", "normal", "hover", "press");
    connect(ui->chatEdit, &MessageTextEdit::send, this, &ChatPage::on_send_btn_clicked);
    connect(ui->file_lb, &ClickLabel::clicked, this, &ChatPage::chooseFile);
    auto manager = FileTransferManager::Getinstance();
    connect(manager.get(), &FileTransferManager::transferRegistered, this,
            [this](const QString& local, const QString& id) {
                auto bubble = _file_bubbles.take(local);
                if (bubble) {
                    bubble->setTransferId(id);
                    _file_bubbles[id] = bubble;
                }
            });
    connect(manager.get(), &FileTransferManager::progressChanged, this,
            [this](const QString& id, qint64 cur, qint64 total) {
                if (_file_bubbles.contains(id))
                    _file_bubbles[id]->setProgress(cur, total);
            });
    connect(manager.get(), &FileTransferManager::transferFinished, this,
            [this](const QString& id, const QString& path) {
                if (_file_bubbles.contains(id))
                    _file_bubbles[id]->setFinished(path);
            });
    connect(manager.get(), &FileTransferManager::transferFailed, this,
            [this](const QString& id, const QString& reason) {
                if (_file_bubbles.contains(id))
                    _file_bubbles[id]->setFailed(reason);
            });
    connect(manager.get(), &FileTransferManager::transferAvailable, this,
            [this](const QJsonObject& item) {
                if (_user_info && item["fromuid"].toInt() == _user_info->_uid)
                    appendFileBubble(item, ChatRole::Other, true);
            });
}

ChatPage::~ChatPage() {
    delete ui;
}

void ChatPage::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ChatPage::SetUserInfo(std::shared_ptr<UserInfo> user_info) {
    _user_info = user_info;
    // 设置ui界面
    ui->title_lb->setText(_user_info->_name);
    _file_bubbles.clear();
    ui->chat_data_list->removeAllItem();
    for (auto& msg : user_info->_chat_msgs) {
        AppendChatMsg(msg);
    }
    for (const auto& file : FileTransferManager::Getinstance()->availableForPeer(user_info->_uid)) {
        const bool incoming = file["fromuid"].toInt() == user_info->_uid;
        appendFileBubble(file, incoming ? ChatRole::Other : ChatRole::Self, incoming);
    }
}

void ChatPage::appendFileBubble(const QJsonObject& metadata, ChatRole role, bool incoming) {
    const auto id = metadata["id"].toString();
    if (_file_bubbles.contains(id))
        return;
    auto* item = new ChatItemBase(role);
    auto self = UserMgr::Getinstance()->GetUserInfo();
    if (role == ChatRole::Self) {
        item->setUserName(self->_name);
        item->setUserIcon(QPixmap(self->_icon));
    } else {
        item->setUserName(_user_info ? _user_info->_name : QString());
        item->setUserIcon(QPixmap(_user_info ? _user_info->_icon : QString()));
    }
    auto* bubble = new FileBubble(metadata, role, incoming, this);
    item->setWidget(bubble);
    ui->chat_data_list->appendChatItem(item);
    _file_bubbles[id] = bubble;
    connect(bubble, &FileBubble::cancelRequested, FileTransferManager::Getinstance().get(),
            &FileTransferManager::cancel);
    connect(bubble, &FileBubble::downloadRequested, this, [this, bubble](const QJsonObject& value) {
        const auto path =
            QFileDialog::getSaveFileName(this, tr("保存文件"), value["name"].toString());
        if (!path.isEmpty())
            FileTransferManager::Getinstance()->startDownload(value, path);
    });
}

void ChatPage::chooseFile(QString, ClickLbState) {
    ui->file_lb->ResetNormalState();
    if (!_user_info)
        return;
    const auto path = QFileDialog::getOpenFileName(this, tr("选择要发送的文件"));
    if (path.isEmpty())
        return;
    QFileInfo info(path);
    QJsonObject metadata{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                         {"name", info.fileName()},
                         {"total_size", info.size()}};
    const auto local = FileTransferManager::Getinstance()->startUpload(path, _user_info->_uid);
    if (local.isEmpty())
        return;
    metadata["id"] = local;
    appendFileBubble(metadata, ChatRole::Self, false);
}

void ChatPage::AppendChatMsg(std::shared_ptr<TextChatData> msg) {
    auto self_info = UserMgr::Getinstance()->GetUserInfo();
    ChatRole role;
    // todo... 添加聊天显示
    if (msg->_from_uid == self_info->_uid) {
        role = ChatRole::Self;
        ChatItemBase* pChatItem = new ChatItemBase(role);

        pChatItem->setUserName(self_info->_name);
        pChatItem->setUserIcon(QPixmap(self_info->_icon));
        QWidget* pBubble = nullptr;
        pBubble = new TextBuble(role, msg->_msg_content);
        pChatItem->setWidget(pBubble);
        ui->chat_data_list->appendChatItem(pChatItem);
    } else {
        role = ChatRole::Other;
        ChatItemBase* pChatItem = new ChatItemBase(role);
        auto friend_info = UserMgr::Getinstance()->GetFriendById(msg->_from_uid);
        if (friend_info == nullptr) {
            return;
        }
        pChatItem->setUserName(friend_info->_name);
        pChatItem->setUserIcon(QPixmap(friend_info->_icon));
        QWidget* pBubble = nullptr;
        pBubble = new TextBuble(role, msg->_msg_content);
        pChatItem->setWidget(pBubble);
        ui->chat_data_list->appendChatItem(pChatItem);
    }
}

void ChatPage::on_send_btn_clicked() {
    if (!_user_info)
        return;
    const auto user = UserMgr::Getinstance()->GetUserInfo();
    if (!user)
        return;
    const auto messages = ui->chatEdit->getMsgList();
    QJsonArray texts;
    for (const auto& message : messages) {
        if (message.msgFlag == "text" && !message.content.isEmpty())
            texts.append(QJsonObject{{"msgid", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                     {"content", message.content}});
    }
    const QJsonObject request{
        {"fromuid", user->_uid}, {"touid", _user_info->_uid}, {"text_array", texts}};
    // Save the entire draft transactionally before clearing it or displaying bubbles.
    if (!texts.isEmpty() && !TcpMgr::Getinstance()->enqueueText(request))
        return;
    int textIndex = 0;
    for (const auto& message : messages) {
        if (message.msgFlag == "text" && !message.content.isEmpty()) {
            const auto id = texts[textIndex++].toObject()["msgid"].toString();
            auto text =
                std::make_shared<TextChatData>(id, message.content, user->_uid, _user_info->_uid);
            emit sig_append_send_chat_msg(text);
            AppendChatMsg(text);
        } else if (message.msgFlag == "image") {
            auto* item = new ChatItemBase(ChatRole::Self);
            item->setUserName(user->_name);
            item->setUserIcon(QPixmap(user->_icon));
            item->setWidget(new PictureBubble(QPixmap(message.content), ChatRole::Self, this));
            ui->chat_data_list->appendChatItem(item);
        }
    }
    ui->chatEdit->clearMessage();
}

void ChatPage::sendMessage() {
    auto pTextEdit = ui->chatEdit;
    ChatRole role = ChatRole::Self;
    QString userName = UserMgr::Getinstance()->GetName();
    QString userIcon = UserMgr::Getinstance()->GetIcon();

    const QVector<MsgInfo>& msgList = pTextEdit->getMsgList();
    for (int i = 0; i < msgList.size(); ++i) {
        QString type = msgList[i].msgFlag;
        ChatItemBase* pChatItem = new ChatItemBase(role);
        pChatItem->setUserName(userName);
        pChatItem->setUserIcon(QPixmap(userIcon));
        QWidget* pBubble = nullptr;
        if (type == "text") {
            pBubble = new TextBuble(role, msgList[i].content);
        } else if (type == "image") {
            auto pixmap = QPixmap(msgList[i].content);
            pBubble = new PictureBubble(pixmap, role, this);
        } else if (type == "file") {
        }
        if (pBubble != nullptr) {
            pChatItem->setWidget(pBubble);
            ui->chat_data_list->appendChatItem(pChatItem);
        }
    }
}
