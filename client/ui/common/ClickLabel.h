#pragma once
#include <QLabel>
#include "global.h"
class ClickLabel : public QLabel
{
    Q_OBJECT
public:
    ClickLabel(QWidget* parent);
    virtual void mousePressEvent(QMouseEvent* ev) override;
    virtual void mouseReleaseEvent(QMouseEvent* ev) override;
    virtual void enterEvent(QEvent* event) override;
    virtual void leaveEvent(QEvent* event) override;
    void SetState(QString normal = "", QString hover = "", QString press = "",
        QString select = "", QString select_hover = "", QString select_press = "");

    ClickLbState GetCurState();
    bool SetCurState(ClickLbState state);
    void ResetNormalState();
protected:

private:
    QString _normal;
    QString _normal_hover;
    QString _normal_press;

    QString _selected;
    QString _selected_hover;
    QString _selected_press;

    ClickLbState _curstate;
signals:
    void clicked(QString, ClickLbState);
};
