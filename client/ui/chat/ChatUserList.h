#pragma once
#include "global.h"
#include "usermgr.h"
#include <QCoreApplication>
#include <QEvent>
#include <QListWidget>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <iostream>
class ChatUserList : public QListWidget {
    Q_OBJECT
  public:
    ChatUserList(QWidget* parent = nullptr);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    bool _load_pending;
  signals:
    void sig_loading_chat_user();
};
