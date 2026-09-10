#pragma once

#include "ClickedBtn.h"
#include "ui_FindFailWidget.h"
#include <QDialog>
QT_BEGIN_NAMESPACE
namespace Ui {
class FindFailWidgetClass;
};
QT_END_NAMESPACE

class FindFailWidget : public QDialog {
    Q_OBJECT

  public:
    FindFailWidget(QWidget* parent = nullptr);
    ~FindFailWidget();
  public slots:
    void fail_sure_btn_clicked();

  private:
    Ui::FindFailWidgetClass* ui;
};
