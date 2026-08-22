#pragma once
/*
    author: which_w
    data:2025-3-8
    brief:主窗口

*/
#include <QtWidgets/QMainWindow>
#include "ui_MainWindow.h"
#include "LoginDialog.h"
#include "RegisterDialog.h"
#include "ResetDialog.h"
#include "ChatDialog.h"
#include "TcpMgr.h"
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindowClass; };
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
public slots:
    void slot_switch_Register();
    void slot_switch_Login();
	void slot_switch_Reset();
    void slot_switch_Login_2();
	void slot_switch_Chat();
private:
    Ui::MainWindowClass *ui;
    LoginDialog* _loginDialog;
    RegisterDialog* _RegisterDialog;
	ResetDialog* _resetDialog;
	ChatDialog* _chatDialog;
};
