#include "TimerBtn.h"

TimerBtn::TimerBtn(QWidget* parent):QPushButton(parent),_count(10)
{
	_timer = new QTimer(this);
	codec = QTextCodec::codecForName("GBK");
	connect(_timer, &QTimer::timeout, [this]() {
		_count--;
		if (_count <= 0) {
			_timer->stop();
			_count = 10;
			this->setEnabled(true);
			this->setText(codec->toUnicode("获取"));
			return;
		}
		this->setText(QString::number(_count));
		}
	);

}

TimerBtn::~TimerBtn()
{
	_timer->stop();
}

void TimerBtn::mouseReleaseEvent(QMouseEvent* e)
{
	if (e->button() == Qt::LeftButton) {
		qDebug() << "点击事件已开始";
		this->setEnabled(false);
		this->setText(QString::number(_count));
		_timer->start(1000);
		emit clicked();
	}
	// 调用基类的mouseReleaseEvent以确保正常的事件处理（如点击效果）
	QPushButton::mouseReleaseEvent(e);

}
