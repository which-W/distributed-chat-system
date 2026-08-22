#pragma once
#include <QFrame>
#include "global.h"
#include <QHBoxLayout>
#include <QPainter>
class BubbleFrame : public QFrame
{
	Q_OBJECT
public:
    BubbleFrame(ChatRole role, QWidget* parent = nullptr);
    void setMargin(int margin);
    void setWidget(QWidget* w);
protected:
    void paintEvent(QPaintEvent* e);
private:
    QHBoxLayout* m_pHLayout;
    ChatRole m_role;
    int      m_margin;

};
