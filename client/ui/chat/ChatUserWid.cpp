#include "AvatarLoader.h"
#include "ChatUserWid.h"
#include "ChatGraphics.h"

ChatUserWid::ChatUserWid(QWidget* parent) : ListItemBase(parent), ui(new Ui::ChatUserWidClass()) {
    ui->setupUi(this);
    SetItemType(ListItemType::CHAT_USER_ITEM);
}

ChatUserWid::~ChatUserWid() {
    delete ui;
}

void ChatUserWid::SetInfo(std::shared_ptr<FriendInfo> friend_info) {
    _user_info = std::make_shared<UserInfo>(friend_info);
    // ????
    QPixmap pixmap(_user_info->_icon);

    // ????????
    ui->icon_lb->setPixmap(
        roundAvatar(pixmap));
    ui->icon_lb->setScaledContents(true);
    if (_user_info->_uid > 0) AvatarLoader::instance().bind(ui->icon_lb,_user_info->_uid,_user_info->_icon,48);

    ui->user_name_lb->setText(_user_info->_name);
    ui->user_data_lb->setText(_user_info->_last_msg);
}

void ChatUserWid::SetInfo(std::shared_ptr<UserInfo> user_info) {
    _user_info = user_info;
    // ????
    QPixmap pixmap(_user_info->_icon);

    // ????????
    ui->icon_lb->setPixmap(
        roundAvatar(pixmap));
    ui->icon_lb->setScaledContents(true);
    if (_user_info->_uid > 0) AvatarLoader::instance().bind(ui->icon_lb,_user_info->_uid,_user_info->_icon,48);

    ui->user_name_lb->setText(_user_info->_name);
    ui->user_data_lb->setText(_user_info->_last_msg);
}

std::shared_ptr<UserInfo> ChatUserWid::GetUserInfo() {
    return _user_info;
}

void ChatUserWid::updateFileSummary(const QString& name) {
    _user_info->_last_msg = tr("[??] %1").arg(name);
    ui->user_data_lb->setTextFormat(Qt::PlainText);
    ui->user_data_lb->setText(_user_info->_last_msg);
    ui->user_data_lb->setToolTip(_user_info->_last_msg);
}

void ChatUserWid::updateLastMsg(std::vector<std::shared_ptr<TextChatData>> msgs) {
    QString last_msg = "";
    for (auto& msg : msgs) {
        last_msg = msg->_msg_content;
        AppendBoundedChatMessage(_user_info->_chat_msgs, msg);
    }

    _user_info->_last_msg = last_msg;
    ui->user_data_lb->setText(_user_info->_last_msg);
}
