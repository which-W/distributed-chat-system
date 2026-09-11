#include "ConUserItem.h"
#include "ChatGraphics.h"

ConUserItem::ConUserItem(QWidget* parent) : ListItemBase(parent), ui(new Ui::ConUserItemClass()) {
    ui->setupUi(this);
    ui->horizontalLayout->setContentsMargins(12, 8, 12, 8);
    ui->horizontalLayout->setSpacing(12);
    ui->horizontalSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    ui->horizontalSpacer_2->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    ui->user_name_lb->setMinimumWidth(0);
    ui->user_name_lb->setMaximumWidth(QWIDGETSIZE_MAX);
    SetItemType(ListItemType::CONTACT_USER_ITEM);
    ui->red_point->raise();
    ShowRedPoint(false);
    // 安装点击事件（假设整个 item 可以点击）
    this->installEventFilter(this);
}

ConUserItem::~ConUserItem() {
    delete ui;
}

QSize ConUserItem::sizeHint() const {
    return QSize(250, 80); // 返回自定义的尺寸
}

void ConUserItem::SetInfo(std::shared_ptr<AuthInfo> auth_info) {
    _info = std::make_shared<UserInfo>(auth_info);
    QPixmap pixmap(_info->_icon);

    // 设置图片自动缩放
    ui->icon_lb->setPixmap(
        roundAvatar(pixmap));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::SetInfo(int uid, QString name, QString icon) {
    _info = std::make_shared<UserInfo>(uid, name, name, icon, 0);

    // 加载图片
    QPixmap pixmap(_info->_icon);

    // 设置图片自动缩放
    ui->icon_lb->setPixmap(
        roundAvatar(pixmap));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::ShowRedPoint(bool show) {
    if (show) {
        ui->red_point->show();
    } else {
        ui->red_point->hide();
    }
}

std::shared_ptr<UserInfo> ConUserItem::GetInfo() {
    return _info;
}
