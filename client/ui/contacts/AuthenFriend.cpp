#include "AuthenFriend.h"
#include "TcpMgr.h"
AuthenFriend::AuthenFriend(QWidget* parent)
    : FriendDialog(tr("好友邀请"), tr("接受邀请后，你们就可以互相发送消息和文件。"), parent) {
    invitation_ = addHint(QString());
    invitation_->setObjectName("invitationMessage");
    addHint(tr("好友备注（仅自己可见）"));
    remark_ = new QLineEdit(this);
    remark_->setObjectName("remarkEdit");
    remark_->setMaxLength(64);
    body->addWidget(remark_);
    status_ = addHint(tr("确认后将添加到你的联系人。"), "operationStatus");
    accept_ = addActions(tr("接受邀请"));
    connect(accept_, &QPushButton::clicked, this, &AuthenFriend::SlotApplySure);
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_friend_operation, this,
            [this](int request, int peer, bool ok, const QString& message) {
                if (!pending_ || !info_ || request != Req::ID_AUTH_FRIEND_REQ || peer != info_->_uid) return;
                pending_ = false;
                status_->setText(message);
                accept_->setText(ok ? tr("已成为好友") : tr("重试接受"));
                accept_->setEnabled(!ok);
                remark_->setReadOnly(ok);
            });
}
void AuthenFriend::SetApplyInfo(std::shared_ptr<ApplyInfo> info) {
    info_ = std::move(info);
    setProfile(info_->_uid, info_->_name, info_->_icon);
    invitation_->setText(info_->_desc.isEmpty() ? tr("对方请求添加你为好友。") : info_->_desc);
    remark_->setText(info_->_name);
}
void AuthenFriend::SlotApplySure() {
    if (!info_ || pending_) return;
    if (remark_->text().toUtf8().size() > 192) { status_->setText(tr("备注太长，请缩短后重试。")); return; }
    pending_ = true;
    accept_->setEnabled(false);
    accept_->setText(tr("正在接受…"));
    status_->setText(tr("正在等待服务器确认…"));
    TcpMgr::Getinstance()->submitFriendOperation(Req::ID_AUTH_FRIEND_REQ, info_->_uid, remark_->text().trimmed());
}
