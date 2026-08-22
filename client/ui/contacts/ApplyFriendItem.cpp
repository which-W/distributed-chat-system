#include "ApplyFriendItem.h"

ApplyFriendItem::ApplyFriendItem(QWidget *parent)
	: ListItemBase(parent)
	, ui(new Ui::ApplyFriendItem())
{
    ui->setupUi(this);
    SetItemType(ListItemType::APPLY_FRIEND_ITEM);
    ui->addBtn->SetState("normal", "hover", "press");
    ui->addBtn->hide();
    connect(ui->addBtn, &ClickedBtn::clicked, [this]() {
        emit this->sig_auth_friend(_apply_info);
        });
    connect(ui->addBtn, &ClickedBtn::click, this, &ApplyFriendItem::slot_ShowAddBtn);
}

ApplyFriendItem::~ApplyFriendItem()
{
	delete ui;
}

void ApplyFriendItem::SetInfo(std::shared_ptr<ApplyInfo> apply_info)
{
    _apply_info = apply_info;
    // 加载图片
    QPixmap pixmap(_apply_info->_icon);

    // 设置图片自动缩放
    ui->icon_lb->setPixmap(pixmap.scaled(ui->icon_lb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(_apply_info->_name);
    QString _msg = _apply_info->_desc;
    QByteArray msgBytes = _msg.toUtf8();
    QString displayMsg = _msg;
    int maxBytes = 50;

    if (msgBytes.size() > maxBytes) {
        // 从前面截取部分字节，再转回字符串
        QString truncated = QString::fromUtf8(msgBytes.left(maxBytes));
        displayMsg = truncated + "...";
    }

    ui->user_chat_lb->setText(displayMsg);

}

void ApplyFriendItem::ShowAddBtn(bool bshow)
{
    if (bshow) {
        ui->addBtn->show();
        ui->already_add_lb->hide();
        _added = false;
    }
    else {
        ui->addBtn->hide();
        ui->already_add_lb->show();
        _added = true;
    }
}

int ApplyFriendItem::GetUid() {
    return _apply_info->_uid;
}

void ApplyFriendItem::slot_ShowAddBtn()
{
    ShowAddBtn(false);
}
