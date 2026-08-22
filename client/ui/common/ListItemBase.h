#pragma once
#include <QWidget>
#include "global.h"
class ListItemBase : public QWidget
{
	Q_OBJECT
public:
public:
    explicit ListItemBase(QWidget* parent = nullptr);
    void SetItemType(ListItemType itemType);
    ~ListItemBase();
    ListItemType GetItemType();

private:
    ListItemType _itemType;

public slots:

signals:

};
