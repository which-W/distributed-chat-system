#pragma once
#include "ElaPushButton.h"
#include <QDebug>
#include <QMouseEvent>
#include <QTimer>
#include <memory>
class TimerBtn : public ElaPushButton {
  public:
    TimerBtn(QWidget* parent = nullptr);
    ~TimerBtn();
    void mouseReleaseEvent(QMouseEvent* e) override;

  private:
    QTimer* _timer;
    int _count;
};
