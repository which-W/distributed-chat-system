#include "ApplyFriend.h"
#include "TcpMgr.h"
#include "usermgr.h"

ApplyFriend::ApplyFriend(QWidget* parent)
    : FriendDialog(tr("添加朋友"), tr("发送一份邀请，开启你们的对话。"), parent) {
    addHint(tr("你的身份"));
    addHint(tr("%1  ·  UID %2").arg(UserMgr::Getinstance()->GetName())
                .arg(UserMgr::Getinstance()->GetUid()), "invitationIdentity");
    addHint(tr("对方会收到你的名字、头像和 UID。接受邀请后，你们就可以互相发送消息和文件。"));
    status_ = addHint(tr("申请会保存在对方的「新的朋友」中。"), "operationStatus");
    send_ = addActions(tr("发送好友申请"));
    connect(send_, &QPushButton::clicked, this, &ApplyFriend::SlotApplySure);
    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_friend_operation, this,
            [this](int request, int peer, bool ok, const QString& message) {
                if (!pending_ || !info_ || request != Req::ID_ADD_FRIEND_REQ || peer != info_->_uid) return;
                pending_ = false;
                status_->setText(message);
                send_->setEnabled(!ok);
                send_->setText(ok ? tr("申请已发送") : tr("重试发送"));
            });
}
void ApplyFriend::SetSearchInfo(std::shared_ptr<SearchInfo> info) {
    info_ = std::move(info);
    setProfile(info_->_uid, info_->_name, info_->_icon);
}
void ApplyFriend::SlotApplySure() {
    if (!info_ || pending_) return;
    pending_ = true;
    send_->setEnabled(false);
    send_->setText(tr("正在发送…"));
    status_->setText(tr("正在等待服务器确认…"));
    TcpMgr::Getinstance()->submitFriendOperation(Req::ID_ADD_FRIEND_REQ, info_->_uid, {});
}
