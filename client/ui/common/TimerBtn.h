#pragma once
#include "ElaPushButton.h"
#include <QTimer>
#include <memory>
#include <QMouseEvent>
#include <QDebug>
class TimerBtn : public ElaPushButton
{
public:
	TimerBtn(QWidget* parent = nullptr);
	~TimerBtn();
	void mouseReleaseEvent(QMouseEvent* e) override;


private:
	QTimer* _timer;
	int _count;
};
