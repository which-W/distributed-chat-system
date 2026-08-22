#include "GroupTipItem.h"

GroupTipItem::GroupTipItem(QWidget *parent)
	: ListItemBase(parent)
	, ui(new Ui::GroupTipItemClass())
{
	ui->setupUi(this);
	SetItemType(ListItemType::GROUP_TIP_ITEM);
}

GroupTipItem::~GroupTipItem()
{
	delete ui;
}

QSize GroupTipItem::sizeHint() const
{
	return QSize(250,30);
}

void GroupTipItem::SetGroupTip(QString str)
{
	ui->label->setText(str);
	// 让 label 高度自适应父控件高度
	ui->label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
