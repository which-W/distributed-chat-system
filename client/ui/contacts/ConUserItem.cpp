#include "AvatarLoader.h"
#include "ConUserItem.h"
#include "ChatGraphics.h"

namespace {
QPixmap invitationIcon() {
    QPixmap icon(80, 80);
    icon.setDevicePixelRatio(2);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(40.0 / 24, 40.0 / 24);
    painter.setPen(QPen(QColor("#0866ff"), 1.8, Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(QRectF(6, 3, 7, 7));
    painter.drawArc(QRectF(3, 12, 13, 12), 0, 180 * 16);
    painter.drawLine(QPointF(19, 12), QPointF(19, 20));
    painter.drawLine(QPointF(15, 16), QPointF(23, 16));
    return icon;
}
}

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
    // ??????????? item ?????
    this->installEventFilter(this);
}

ConUserItem::~ConUserItem() {
    delete ui;
}

QSize ConUserItem::sizeHint() const {
    return QSize(250, 80); // ????????
}

void ConUserItem::SetInfo(std::shared_ptr<AuthInfo> auth_info) {
    _info = std::make_shared<UserInfo>(auth_info);
    QPixmap pixmap(_info->_icon);

    // ????????
    ui->icon_lb->setPixmap(
        roundAvatar(pixmap));
    ui->icon_lb->setScaledContents(true);
    if (_info->_uid > 0) AvatarLoader::instance().bind(ui->icon_lb,_info->_uid,_info->_icon,48);

    ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::SetInfo(int uid, QString name, QString icon) {
    _info = std::make_shared<UserInfo>(uid, name, name, icon, 0);

    // ????
    QPixmap pixmap = uid == 0 ? invitationIcon() : QPixmap(_info->_icon);

    // ????????
    ui->icon_lb->setPixmap(
        roundAvatar(pixmap));
    ui->icon_lb->setScaledContents(true);
    if (_info->_uid > 0) AvatarLoader::instance().bind(ui->icon_lb,_info->_uid,_info->_icon,48);

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
