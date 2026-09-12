#include "FriendInfoPage.h"
#include "ChatGraphics.h"
#include "ThemeManager.h"
#include <QVBoxLayout>
#include <QPushButton>

FriendInfoPage::FriendInfoPage(QWidget* parent) : QWidget(parent) {
    setObjectName("friend_info_page");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch();
    auto* card = new QWidget(this);
    card->setObjectName("contactProfileCard");
    card->setMaximumWidth(420);
    card->setMinimumWidth(300);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* body = new QVBoxLayout(card);
    body->setContentsMargins(32, 28, 32, 28);
    body->setSpacing(14);
    avatar_ = new QLabel(card);
    avatar_->setFixedSize(88, 88);
    body->addWidget(avatar_, 0, Qt::AlignHCenter);
    name_ = new QLabel(card);
    name_->setObjectName("profileName");
    uid_ = new QLabel(card);
    uid_->setObjectName("profileUid");
    nick_ = new QLabel(card);
    remark_ = new QLabel(card);
    for (auto* label : {name_, uid_, nick_, remark_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignCenter);
        body->addWidget(label);
    }
    body->addSpacing(10);
    auto* chat = new QPushButton(tr("发送消息"), card);
    chat->setObjectName("startFriendChat");
    chat->setMinimumHeight(44);
    body->addWidget(chat);
    connect(chat, &QPushButton::clicked, this, [this]() {
        if (_user_info) emit sig_jump_chat_item(_user_info);
    });
    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() { applyTheme(); });
    applyTheme();
}
void FriendInfoPage::SetInfo(std::shared_ptr<UserInfo> user) {
    _user_info = user;
    if (!user) return;
    avatar_->setPixmap(roundAvatar(QPixmap(user->_icon), 88));
    name_->setText(user->_name);
    uid_->setText(tr("UID  %1").arg(user->_uid));
    nick_->setText(tr("昵称   %1").arg(user->_nick.isEmpty() ? user->_name : user->_nick));
    const auto friendInfo = UserMgr::Getinstance()->GetFriendById(user->_uid);
    const auto remark = friendInfo ? friendInfo->_back : QString();
    remark_->setText(tr("备注   %1").arg(remark.isEmpty() ? tr("未设置") : remark));
}
void FriendInfoPage::applyTheme() {
    const bool dark = ThemeManager::instance().themeMode() == ElaThemeType::Dark;
    setStyleSheet(QString(
        "QWidget#contactProfileCard { background:%1; border:1px solid %2; border-radius:24px; }"
        "QLabel { color:%3; background:transparent; font-size:14px; }"
        "QLabel#profileName { font-size:25px; font-weight:700; }"
        "QLabel#profileUid { color:%4; font-size:12px; }"
        "QPushButton#startFriendChat { color:white; background:#0866ff; border:0; border-radius:14px; padding:8px 32px; font-size:14px; }"
        "QPushButton#startFriendChat:hover { background:#287dff; }")
        .arg(dark ? "#252a34" : "#ffffff", dark ? "#384150" : "#e4eaf4",
             dark ? "#edf2fa" : "#1e2b42", dark ? "#9eacc2" : "#8190a7"));
}
