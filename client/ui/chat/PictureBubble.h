#pragma once
#include "BubbleFrame.h"
#include "global.h"
#include <QLabel>
#include <QWidget>
class PictureBubble : public BubbleFrame {
    Q_OBJECT
  public:
    PictureBubble(const QPixmap& picture, ChatRole role, QWidget* parent);
};
