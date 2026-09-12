#include "FindSuccessWidght.h"
#include "ApplyFriend.h"
FindSuccessWidght::FindSuccessWidght(QWidget* parent)
    : FriendDialog(tr("找到朋友"), tr("确认对方的资料，再发送好友申请。"), parent) {
    description_ = addHint(QString());
    auto* add = addActions(tr("添加为好友"));
    add->setObjectName("add_friend_btn");
    connect(add, &QPushButton::clicked, this, [this]() {
        if (!info_) return;
        hide();
        auto* dialog = new ApplyFriend(parentWidget());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->SetSearchInfo(info_);
        dialog->show();
    });
}
void FindSuccessWidght::SetSearchInfo(std::shared_ptr<SearchInfo> info) {
    info_ = std::move(info);
    setProfile(info_->_uid, info_->_name, info_->_icon);
    description_->setText(info_->_desc.isEmpty() ? tr("对方还没有填写个人简介。") : info_->_desc);
}
