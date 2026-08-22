#pragma once
#include <QWidget>
#include "global.h"
#include "BubbleFrame.h"
#include <QLabel>
class PictureBubble : public BubbleFrame
{
	Q_OBJECT
public:
	PictureBubble(const QPixmap& picture, ChatRole role, QWidget* parent);
};
