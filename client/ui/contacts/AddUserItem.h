#pragma once

#include "ListItemBase.h"
#include "ui_AddUserItem.h"
#include <QWidget>
QT_BEGIN_NAMESPACE
namespace Ui {
class AddUserItemClass;
};
QT_END_NAMESPACE

class AddUserItem : public ListItemBase {
    Q_OBJECT

  public:
    explicit AddUserItem(QWidget* parent = nullptr);
    ~AddUserItem();
    QSize sizeHint() const override {
        return QSize(200, 70); // 返回自定义的尺寸
    }

  protected:
  private:
    Ui::AddUserItemClass* ui;
};
