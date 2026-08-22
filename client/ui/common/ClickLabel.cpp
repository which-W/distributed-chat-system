#include "ClickLabel.h"
#include <QMouseEvent>

ClickLabel::ClickLabel(QWidget* parent) :QLabel(parent), _curstate(ClickLbState::Normal)
{
    setCursor(Qt::PointingHandCursor);
}

// 处理鼠标点击事件
void ClickLabel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (_curstate == ClickLbState::Normal) {
            // qDebug()<<"PressEvent , change to selected press: "<< _selected_press;
            _curstate = ClickLbState::Selected;
            setProperty("state", _selected_press);
            repolish(this);
            update();

        }
        else {
            // qDebug()<<"PressEvent , change to normal press: "<< _normal_press;
            _curstate = ClickLbState::Normal;
            setProperty("state", _normal_press);
            repolish(this);
            update();
        }
        return;
    }
    // 调用基类的mousePressEvent以保证正常的事件处理
    QLabel::mousePressEvent(event);
}

void ClickLabel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (_curstate == ClickLbState::Normal) {
            // qDebug()<<"ReleaseEvent , change to normal hover: "<< _normal_hover;
            setProperty("state", _normal_hover);
            repolish(this);
            update();

        }
        else {
            //  qDebug()<<"ReleaseEvent , change to select hover: "<< _selected_hover;
            setProperty("state", _selected_hover);
            repolish(this);
            update();
        }
        emit clicked(this->text(), _curstate);
        return;
    }
    // 调用基类的mousePressEvent以保证正常的事件处理
    QLabel::mousePressEvent(event);
}

// 处理鼠标悬停进入事件
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void ClickLabel::enterEvent(QEnterEvent* event) {
#else
void ClickLabel::enterEvent(QEvent* event) {
#endif
    // 在这里处理鼠标悬停进入的逻辑
    if (_curstate == ClickLbState::Normal) {
        // qDebug()<<"enter , change to normal hover: "<< _normal_hover;
        setProperty("state", _normal_hover);
        repolish(this);
        update();

    }
    else {
        // qDebug()<<"enter , change to selected hover: "<< _selected_hover;
        setProperty("state", _selected_hover);
        repolish(this);
        update();
    }

    QLabel::enterEvent(event);
}

// 处理鼠标悬停离开事件
void ClickLabel::leaveEvent(QEvent* event) {
    // 在这里处理鼠标悬停离开的逻辑
    if (_curstate == ClickLbState::Normal) {
        // qDebug()<<"leave , change to normal : "<< _normal;
        setProperty("state", _normal);
        repolish(this);
        update();

    }
    else {
        // qDebug()<<"leave , change to normal hover: "<< _selected;
        setProperty("state", _selected);
        repolish(this);
        update();
    }
    QLabel::leaveEvent(event);
}

void ClickLabel::SetState(QString normal, QString hover, QString press,
    QString select, QString select_hover, QString select_press)
{
    _normal = normal;
    _normal_hover = hover;
    _normal_press = press;

    _selected = select;
    _selected_hover = select_hover;
    _selected_press = select_press;

    setProperty("state", normal);
    repolish(this);
}

ClickLbState ClickLabel::GetCurState() {
    return _curstate;
}

bool ClickLabel::SetCurState(ClickLbState state)
{
    _curstate = state;
    if (_curstate == ClickLbState::Normal) {
        setProperty("state", _normal);
        repolish(this);
    }
    else if (_curstate == ClickLbState::Selected) {
        setProperty("state", _selected);
        repolish(this);
    }

    return true;
}

void ClickLabel::ResetNormalState()
{
    _curstate = ClickLbState::Normal;
    setProperty("state", _normal);
    repolish(this);
}
