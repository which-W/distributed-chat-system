#include "ChatPage.h"
#include <QFileDialog>
#include <QFileInfo>

ChatPage::ChatPage(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::ChatPageClass())
{
	ui->setupUi(this);
	//设置按钮样式
	ui->send_btn->SetState("normal", "hover", "press");

	//设置图标样式
	ui->emo_lb->SetState("normal", "hover", "press", "normal", "hover", "press");
	ui->file_lb->SetState("normal", "hover", "press", "normal", "hover", "press");
	connect(ui->chatEdit, &MessageTextEdit::send, this, &ChatPage::on_send_btn_clicked);
	connect(ui->file_lb, &ClickLabel::clicked, this, &ChatPage::chooseFile);
	auto manager=FileTransferManager::Getinstance();
	connect(manager.get(),&FileTransferManager::transferRegistered,this,[this](const QString& local,const QString& id){
		auto bubble=_file_bubbles.take(local);if(bubble){bubble->setTransferId(id);_file_bubbles[id]=bubble;}});
	connect(manager.get(),&FileTransferManager::progressChanged,this,[this](const QString& id,qint64 cur,qint64 total){if(_file_bubbles.contains(id))_file_bubbles[id]->setProgress(cur,total);});
	connect(manager.get(),&FileTransferManager::transferFinished,this,[this](const QString& id,const QString& path){if(_file_bubbles.contains(id))_file_bubbles[id]->setFinished(path);});
	connect(manager.get(),&FileTransferManager::transferFailed,this,[this](const QString& id,const QString& reason){if(_file_bubbles.contains(id))_file_bubbles[id]->setFailed(reason);});
	connect(manager.get(),&FileTransferManager::transferAvailable,this,[this](const QJsonObject& item){if(_user_info&&item["fromuid"].toInt()==_user_info->_uid)appendFileBubble(item,ChatRole::Other,true);});
}

ChatPage::~ChatPage()
{
	delete ui;
}

void ChatPage::paintEvent(QPaintEvent* event)
{
	QStyleOption opt;
	opt.initFrom(this);
	QPainter p(this);
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ChatPage::SetUserInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
    //设置ui界面
    ui->title_lb->setText(_user_info->_name);
	_file_bubbles.clear();
    ui->chat_data_list->removeAllItem();
    for (auto& msg : user_info->_chat_msgs) {
        AppendChatMsg(msg);
    }
	for(const auto& file:FileTransferManager::Getinstance()->availableForPeer(user_info->_uid)) {
		const bool incoming=file["fromuid"].toInt()==user_info->_uid;
		appendFileBubble(file,incoming?ChatRole::Other:ChatRole::Self,incoming);
	}
}

void ChatPage::appendFileBubble(const QJsonObject& metadata,ChatRole role,bool incoming)
{
	const auto id=metadata["id"].toString();if(_file_bubbles.contains(id))return;
	auto* item=new ChatItemBase(role);auto self=UserMgr::Getinstance()->GetUserInfo();
	if(role==ChatRole::Self){item->setUserName(self->_name);item->setUserIcon(QPixmap(self->_icon));}
	else{item->setUserName(_user_info?_user_info->_name:QString());item->setUserIcon(QPixmap(_user_info?_user_info->_icon:QString()));}
	auto* bubble=new FileBubble(metadata,role,incoming,this);item->setWidget(bubble);ui->chat_data_list->appendChatItem(item);_file_bubbles[id]=bubble;
	connect(bubble,&FileBubble::cancelRequested,FileTransferManager::Getinstance().get(),&FileTransferManager::cancel);
	connect(bubble,&FileBubble::downloadRequested,this,[this,bubble](const QJsonObject& value){
		const auto path=QFileDialog::getSaveFileName(this,tr("保存文件"),value["name"].toString());
		if(!path.isEmpty())FileTransferManager::Getinstance()->startDownload(value,path);
	});
}

void ChatPage::chooseFile(QString,ClickLbState)
{
	ui->file_lb->ResetNormalState();if(!_user_info)return;
	const auto path=QFileDialog::getOpenFileName(this,tr("选择要发送的文件"));if(path.isEmpty())return;
	QFileInfo info(path);QJsonObject metadata{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
		{"name",info.fileName()},{"total_size",info.size()}};
	const auto local=FileTransferManager::Getinstance()->startUpload(path,_user_info->_uid);if(local.isEmpty())return;
	metadata["id"]=local;appendFileBubble(metadata,ChatRole::Self,false);
}

void ChatPage::AppendChatMsg(std::shared_ptr<TextChatData> msg)
{
    auto self_info = UserMgr::Getinstance()->GetUserInfo();
    ChatRole role;
    //todo... 添加聊天显示
    if (msg->_from_uid == self_info->_uid) {
        role = ChatRole::Self;
        ChatItemBase* pChatItem = new ChatItemBase(role);

        pChatItem->setUserName(self_info->_name);
        pChatItem->setUserIcon(QPixmap(self_info->_icon));
        QWidget* pBubble = nullptr;
        pBubble = new TextBuble(role, msg->_msg_content);
        pChatItem->setWidget(pBubble);
        ui->chat_data_list->appendChatItem(pChatItem);
    }
    else {
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

void ChatPage::on_send_btn_clicked()
{
    if (_user_info == nullptr) {
        qDebug() << "friend_info is empty";
        return;
    }
    //获取当前用户信息
    auto user_info = UserMgr::Getinstance()->GetUserInfo();
    auto pTextEdit = ui->chatEdit;
    ChatRole role = ChatRole::Self;
    QString userName = user_info->_name;
    QString userIcon = user_info->_icon;
    //开始输入消息
    const QVector<MsgInfo>& msgList = pTextEdit->getMsgList();
    QJsonObject textObj;
    QJsonArray textArray;
    int txt_size = 0;

    for (int i = 0; i < msgList.size(); ++i)
    {
        //消息内容长度不合规就跳过
        if (msgList[i].content.length() > 1024) {
            continue;
        }

        QString type = msgList[i].msgFlag;
        ChatItemBase* pChatItem = new ChatItemBase(role);
        pChatItem->setUserName(userName);
        pChatItem->setUserIcon(QPixmap(userIcon));
        QWidget* pBubble = nullptr;

        if (type == "text")
        {
            //生成唯一id
            QUuid uuid = QUuid::createUuid();
            //转为字符串
            QString uuidString = uuid.toString();

            pBubble = new TextBuble(role, msgList[i].content);
            if (txt_size + msgList[i].content.length() > 1024) {
                textObj["fromuid"] = user_info->_uid;
                textObj["touid"] = _user_info->_uid;
                textObj["text_array"] = textArray;
                QJsonDocument doc(textObj);
                QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
                //发送并清空之前累计的文本列表
                txt_size = 0;
                textArray = QJsonArray();
                textObj = QJsonObject();
                //发送tcp请求给chat server
                emit TcpMgr::Getinstance()->sig_send_data(Req::ID_TEXT_CHAT_MSG_REQ, jsonData);
            }

            //将bubble和uid绑定，以后可以等网络返回消息后设置是否送达
            //_bubble_map[uuidString] = pBubble;
            txt_size += msgList[i].content.length();
            QJsonObject obj;
            QByteArray utf8Message = msgList[i].content.toUtf8();
            obj["content"] = QString::fromUtf8(utf8Message);
            obj["msgid"] = uuidString;
            textArray.append(obj);
            auto txt_msg = std::make_shared<TextChatData>(uuidString, obj["content"].toString(),
                user_info->_uid, _user_info->_uid);
            emit sig_append_send_chat_msg(txt_msg);
        }
        else if (type == "image")
        {
            auto pixmap = QPixmap(msgList[i].content);
            pBubble = new PictureBubble(pixmap, role, this);
        }
        else if (type == "file")
        {

        }
        //发送消息
        if (pBubble != nullptr)
        {
            pChatItem->setWidget(pBubble);
            ui->chat_data_list->appendChatItem(pChatItem);
        }

    }

    qDebug() << "textArray is " << textArray;
    //发送给服务器
    textObj["text_array"] = textArray;
    textObj["fromuid"] = user_info->_uid;
    textObj["touid"] = _user_info->_uid;
    QJsonDocument doc(textObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    //发送并清空之前累计的文本列表
    txt_size = 0;
    textArray = QJsonArray();
    textObj = QJsonObject();
    //发送tcp请求给chat server
    emit TcpMgr::Getinstance()->sig_send_data(Req::ID_TEXT_CHAT_MSG_REQ, jsonData);
}

void ChatPage::sendMessage()
{
    auto pTextEdit = ui->chatEdit;
    ChatRole role = ChatRole::Self;
    QString userName = UserMgr::Getinstance()->GetName();
    QString userIcon = UserMgr::Getinstance()->GetIcon();

    const QVector<MsgInfo>& msgList = pTextEdit->getMsgList();
    for (int i = 0; i < msgList.size(); ++i)
    {
        QString type = msgList[i].msgFlag;
        ChatItemBase* pChatItem = new ChatItemBase(role);
        pChatItem->setUserName(userName);
        pChatItem->setUserIcon(QPixmap(userIcon));
        QWidget* pBubble = nullptr;
        if (type == "text")
        {
            pBubble = new TextBuble(role, msgList[i].content);
        }
        else if (type == "image")
        {
			auto pixmap = QPixmap(msgList[i].content);
            pBubble = new PictureBubble(pixmap, role,this);
        }
        else if (type == "file")
        {

        }
        if (pBubble != nullptr)
        {
            pChatItem->setWidget(pBubble);
            ui->chat_data_list->appendChatItem(pChatItem);
        }
    }
}
