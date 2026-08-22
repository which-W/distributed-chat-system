#include "AddUserItem.h"

AddUserItem::AddUserItem(QWidget* parent) :
    ListItemBase(parent),
    ui(new Ui::AddUserItemClass)
{
    ui->setupUi(this);
    SetItemType(ListItemType::ADD_USER_TIP_ITEM);
}

AddUserItem::~AddUserItem()
{
    delete ui;
}
