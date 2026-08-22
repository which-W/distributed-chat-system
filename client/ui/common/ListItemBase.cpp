#include "ListItemBase.h"

ListItemBase::ListItemBase(QWidget* parent)
{
}

void ListItemBase::SetItemType(ListItemType itemType)
{
	_itemType = itemType;
}

ListItemBase::~ListItemBase()
{
}

ListItemType ListItemBase::GetItemType()
{
	return _itemType;
}
