#include "AvatarLoader.h"
#include "ApplyFriendItem.h"
#include "ChatGraphics.h"

ApplyFriendItem::ApplyFriendItem(QWidget* parent)
    : ListItemBase(parent), ui(new Ui::ApplyFriendItem()) {
    ui->setupUi(this);
    SetItemType(ListItemType::APPLY_FRIEND_ITEM);
    ui->addBtn->SetState("normal", "hover", "press");
    ui->addBtn->setText(tr("??"));
    ui->addBtn->hide();
    connect(ui->addBtn, &ClickedBtn::clicked,
            [this]() { emit this->sig_auth_friend(_apply_info); });
}

ApplyFriendItem::~ApplyFriendItem() {
    delete ui;
}

void ApplyFriendItem::SetInfo(std::shared_ptr<ApplyInfo> apply_info) {
    _apply_info = apply_info;
    // ????
    QPixmap pixmap(_apply_info->_icon);

    // ????????
    ui->icon_lb->setPixmap(
        roundAvatar(pixmap));
    ui->icon_lb->setScaledContents(true);
    if (_apply_info->_uid > 0) AvatarLoader::instance().bind(ui->icon_lb,_apply_info->_uid,_apply_info->_icon,48);

    ui->user_name_lb->setTextFormat(Qt::PlainText);
    ui->user_name_lb->setText(_apply_info->_name);
    ui->user_chat_lb->setTextFormat(Qt::PlainText);
    QString _msg = _apply_info->_desc.isEmpty()
        ? tr("UID %1 ? ????????").arg(_apply_info->_uid) : _apply_info->_desc;
    QByteArray msgBytes = _msg.toUtf8();
    QString displayMsg = _msg;
    int maxBytes = 50;

    if (msgBytes.size() > maxBytes) {
        // ????????????????
        QString truncated = QString::fromUtf8(msgBytes.left(maxBytes));
        displayMsg = truncated + "...";
    }

    ui->user_chat_lb->setText(displayMsg);
}

void ApplyFriendItem::ShowAddBtn(bool bshow) {
    if (bshow) {
        ui->addBtn->show();
        ui->already_add_lb->hide();
        _added = false;
    } else {
        ui->addBtn->hide();
        ui->already_add_lb->show();
        _added = true;
    }
}

int ApplyFriendItem::GetUid() {
    return _apply_info->_uid;
}

void ApplyFriendItem::slot_ShowAddBtn() {
    ShowAddBtn(false);
}
