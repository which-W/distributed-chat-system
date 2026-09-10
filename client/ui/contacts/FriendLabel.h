#pragma once

#include "ClickLabel.h"
#include "ui_FriendLabel.h"
#include <QFrame>
#include <QWidget>
QT_BEGIN_NAMESPACE
namespace Ui {
class FriendLabelClass;
};
QT_END_NAMESPACE

class FriendLabel : public QFrame {
    Q_OBJECT
  public:
    explicit FriendLabel(QWidget* parent = nullptr);
    ~FriendLabel();
    void SetText(QString text);
    int Width();
    int Height();
    QString Text();

  private:
    Ui::FriendLabelClass* ui;
    QString _text;
    int _width;
    int _height;
  public slots:
    void slot_close();
  signals:
    void sig_close(QString);
};
