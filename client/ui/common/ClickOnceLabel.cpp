#include "ClickOnceLabel.h"

ClickOnceLabel::ClickOnceLabel(QWidget* parent) : QLabel(parent) {

	setCursor(Qt::PointingHandCursor);
}

void ClickOnceLabel::mouseReleaseEvent(QMouseEvent* ev) {
	if (ev->button() == Qt::LeftButton) {
		emit clicked(this->text());
		return;
	}
	//调用基类的事件处理函数，保证其他事件仍然能够正常处理
	QLabel::mouseReleaseEvent(ev);
}
