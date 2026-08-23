#pragma once
#include "global.h"
#include "ElaPushButton.h"
#include <QEnterEvent>
class ClickedBtn : public ElaPushButton
{
    Q_OBJECT
public:
    ClickedBtn(QWidget* parent = nullptr);
    ~ClickedBtn();
    void SetState(QString nomal, QString hover, QString press);
protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    virtual void enterEvent(QEvent* event) override; // 鼠标进入
#endif
    virtual void leaveEvent(QEvent* event) override;// 鼠标离开
    virtual void mousePressEvent(QMouseEvent* event) override; // 鼠标按下
    virtual void mouseReleaseEvent(QMouseEvent* event) override; // 鼠标释放
private:
    QString _normal;
    QString _hover;
    QString _press;
};
