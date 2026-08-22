#pragma once
#include "global.h"
#include <QWidget>
#include <QListWidget>
#include <QScrollBar>
#include <QEvent>
#include <iostream>
#include <QWheelEvent>
#include "usermgr.h"
#include <QTimer>
#include <QCoreApplication>
class ChatUserList : public QListWidget
{
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
