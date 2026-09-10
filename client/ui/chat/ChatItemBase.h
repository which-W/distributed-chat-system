#pragma once
#include "global.h"
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>
class ChatItemBase : public QWidget {
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
