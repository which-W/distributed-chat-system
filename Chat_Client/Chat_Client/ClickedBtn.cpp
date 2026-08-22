#include "ClickedBtn.h"

ClickedBtn::ClickedBtn(QWidget* parent) :QPushButton(parent)
{
    setCursor(Qt::PointingHandCursor); // 设置光标为小手
	setFocusPolicy(Qt::NoFocus); // 设置按钮不获取焦点
}

ClickedBtn::~ClickedBtn() {

}

void ClickedBtn::SetState(QString normal, QString hover, QString press)
{
    _hover = hover;
    _normal = normal;
    _press = press;
    setProperty("state", normal);
    repolish(this);
    update();
}

void ClickedBtn::enterEvent(QEvent* event)
{
    setProperty("state", _hover);
    repolish(this);
    update();
    QPushButton::enterEvent(event);
}

void ClickedBtn::leaveEvent(QEvent* event)
{
	setProperty("state", _normal);
	repolish(this);
	update();
	// 如果鼠标离开按钮区域，设置状态为normal
	// 这里可以根据需要添加其他逻辑
	QPushButton::leaveEvent(event);
}

void ClickedBtn::mousePressEvent(QMouseEvent* event)
{
    setProperty("state", _press);
    repolish(this);
    update();
    QPushButton::mousePressEvent(event);
}

void ClickedBtn::mouseReleaseEvent(QMouseEvent* event)
{
    setProperty("state", _hover);
    repolish(this);
    update();
    QPushButton::mouseReleaseEvent(event);
}
