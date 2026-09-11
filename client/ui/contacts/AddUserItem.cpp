#include "AddUserItem.h"

AddUserItem::AddUserItem(QWidget* parent) : ListItemBase(parent), ui(new Ui::AddUserItemClass) {
    ui->setupUi(this);
    ui->horizontalLayout->setContentsMargins(14, 8, 14, 8);
    ui->horizontalLayout->setSpacing(10);
    ui->add_tip->setText("+");
    ui->add_tip->setAlignment(Qt::AlignCenter);
    ui->add_tip->setStyleSheet("color: #0866ff; font-size: 26px;");
    ui->right_tip->setText(QString::fromUtf8("›"));
    ui->message_tip->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->message_tip->setText(tr("查找 UID / 用户名"));
    SetItemType(ListItemType::ADD_USER_TIP_ITEM);
}

AddUserItem::~AddUserItem() {
    delete ui;
}
