#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPixmap>
#include "global.h"
class ChatItemBase : public QWidget
{
	Q_OBJECT
public:
    explicit ChatItemBase(ChatRole role, QWidget* parent = nullptr);
    void setUserName(const QString& name);
    void setUserIcon(const QPixmap& icon);
    void setWidget(QWidget* w);

private:
    ChatRole m_role;
    QLabel* m_pNameLabel;
    QLabel* m_pIconLabel;
    QWidget* m_pBubble;

};
