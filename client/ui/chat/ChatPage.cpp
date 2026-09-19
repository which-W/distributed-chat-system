#include "ChatPage.h"
#include "AvatarLoader.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include "ChatGraphics.h"
#include "ElaMessageBar.h"

ChatPage::ChatPage(QWidget* parent) : QWidget(parent), ui(new Ui::ChatPageClass()) {
    ui->setupUi(this);
    ui->title_lb->setText(tr("你的消息"));
    auto* avatar = new QLabel(ui->title_wid);
    avatar->setObjectName("conversationAvatar");
    avatar->setFixedSize(44, 44);
    avatar->hide();
    ui->horizontalLayout_2->insertWidget(0, avatar);
    ui->horizontalLayout_2->setSpacing(14);
    ui->title_wid->setFixedHeight(72);
    ui->verticalLayout_2->setContentsMargins(24, 8, 24, 8);
    ui->verticalLayout->setStretch(1, 1);
    // A single compact composer grows for multiline drafts.
    auto* composer = new QWidget(this);
    composer->setObjectName("composer");
    auto* row = new QHBoxLayout(composer);
    row->setContentsMargins(20, 14, 20, 18);
    row->setSpacing(12);
    row->addWidget(ui->file_lb);
    row->addWidget(ui->emo_lb);
    row->addWidget(ui->chatEdit, 1);
    row->addWidget(ui->send_btn);
    ui->tool_wid->hide();
    ui->send_wid->hide();
    ui->verticalLayout->addWidget(composer);
    ui->chatEdit->setMinimumHeight(48);
    ui->chatEdit->setMaximumHeight(120);
    ui->chatEdit->setPlaceholderText(tr("输入消息…"));
    ui->chatEdit->setToolTip(tr("Enter 发送，Shift+Enter 换行"));
    ui->chatEdit->setEnabled(false);
    ui->send_btn->setFixedSize(64, 40);
    ui->send_btn->setEnabled(false);
    ui->file_lb->setFixedSize(32, 32);
    ui->file_lb->setText("+");
    ui->file_lb->setAlignment(Qt::AlignCenter);
    ui->file_lb->setToolTip(tr("发送文件"));
    ui->emo_lb->setFixedSize(32, 32);
    ui->emo_lb->setPixmap(chatGlyph("smile", QColor("#0866ff"), 28));
    ui->emo_lb->setAlignment(Qt::AlignCenter);
    ui->emo_lb->setToolTip(tr("表情"));
    connect(ui->chatEdit, &QTextEdit::textChanged, this, [this]() {
        const int h = qBound(48, int(ui->chatEdit->document()->size().height()) + 18, 120);
        ui->chatEdit->setFixedHeight(h);
        ui->send_btn->setEnabled(_user_info && !ui->chatEdit->toPlainText().trimmed().isEmpty());
    });
    connect(ui->emo_lb, &ClickLabel::clicked, this, [this]() {
        ui->emo_lb->ResetNormalState();
        if (!_user_info) return;
        QMenu menu(this);
        for (const auto& emoji : {"😀", "😊", "❤️", "👍", "🎉", "🙏"}) {
            auto* action = menu.addAction(QString::fromUtf8(emoji));
            connect(action, &QAction::triggered, this, [this, action]() {
                ui->chatEdit->insertPlainText(action->text());
                ui->chatEdit->setFocus();
            });
        }
        menu.exec(ui->emo_lb->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    auto* empty = new QLabel(tr("选择一位朋友，开始聊天\n点击左上角 + 查找并添加新朋友"), ui->chat_data_list);
    empty->setObjectName("chatEmptyState");
    empty->setAlignment(Qt::AlignCenter);
    ui->chat_data_list->layout()->addWidget(empty);
    ui->chat_data_list->findChild<QScrollArea*>()->hide();
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
                  else if (id.isEmpty())
                      ElaMessageBar::warning(ElaMessageBarType::TopRight, tr("文件未发送"), reason, 5000, window());
            });
    connect(manager.get(), &FileTransferManager::transferAvailable, this,
            [this](const QJsonObject& item) {
                if (!_user_info) return;
                const int self = UserMgr::Getinstance()->GetUid();
                if (item["fromuid"].toInt() == _user_info->_uid && item["touid"].toInt() == self)
                    appendFileBubble(item, ChatRole::Other, true);
                else if (item["fromuid"].toInt() == self && item["touid"].toInt() == _user_info->_uid)
                    appendFileBubble(item, ChatRole::Self, false);
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
    auto* avatar = findChild<QLabel*>("conversationAvatar");
    AvatarLoader::instance().bind(avatar,user_info->_uid,user_info->_icon,44);
    avatar->show();
    if (auto* empty = findChild<QLabel*>("chatEmptyState")) empty->hide();
    ui->chat_data_list->findChild<QScrollArea*>()->show();
    ui->chatEdit->setEnabled(true);
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
    TcpMgr::Getinstance()->refreshFriends();
}

void ChatPage::appendFileBubble(const QJsonObject& metadata, ChatRole role, bool incoming) {
    const auto id = metadata["id"].toString();
    if (_file_bubbles.contains(id))
        return;
    auto* item = new ChatItemBase(role);
    auto self = UserMgr::Getinstance()->GetUserInfo();
    if (role == ChatRole::Self) {
        item->setUserName(self->_name);
        item->setUserAvatar(self->_uid,self->_icon);
    } else {
        item->setUserName(_user_info ? _user_info->_name : QString());
        item->setUserAvatar(_user_info ? _user_info->_uid : 0,_user_info ? _user_info->_icon : QString());
    }
    auto* bubble = new FileBubble(metadata, role, incoming, this);
    const auto localPath = FileTransferManager::Getinstance()->localPathForTransfer(id);
    if (!localPath.isEmpty() || (!incoming && metadata["completed"].toBool())) bubble->setFinished(localPath);
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
    if (_file_bubbles.contains(local)) _file_bubbles[local]->setLocalPreview(path);
}

void ChatPage::AppendChatMsg(std::shared_ptr<TextChatData> msg) {
    auto self_info = UserMgr::Getinstance()->GetUserInfo();
    ChatRole role;
    // todo... 添加聊天显示
    if (msg->_from_uid == self_info->_uid) {
        role = ChatRole::Self;
        ChatItemBase* pChatItem = new ChatItemBase(role);

        pChatItem->setUserName(self_info->_name);
        pChatItem->setUserAvatar(self_info->_uid,self_info->_icon);
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
        pChatItem->setUserAvatar(friend_info->_uid,friend_info->_icon);
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
            item->setUserAvatar(user->_uid,user->_icon);
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
        pChatItem->setUserAvatar(UserMgr::Getinstance()->GetUid(),userIcon);
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
