#pragma once

#include <QWidget>
#include "ui_GroupTipItem.h"
#include "ListItemBase.h"
QT_BEGIN_NAMESPACE
namespace Ui { class GroupTipItemClass; };
QT_END_NAMESPACE

class GroupTipItem : public ListItemBase
{
	Q_OBJECT

public:
    explicit GroupTipItem(QWidget* parent = nullptr);
    ~GroupTipItem();
    QSize sizeHint() const override;
    void SetGroupTip(QString str);
private:
    QString _tip;
    Ui::GroupTipItemClass* ui;
};
