#include "MainWindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindowClass())
{
    ui->setupUi(this);
     _loginDialog = new LoginDialog(this);
     _loginDialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_loginDialog);
    _loginDialog->show();

    connect(_loginDialog, &LoginDialog::switch_RegisterDialog, this, &MainWindow::slot_switch_Register);
	connect(_loginDialog, &LoginDialog::sig_switch_Reset, this, &MainWindow::slot_switch_Reset);

    connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_swich_chatdlg, this, &MainWindow::slot_switch_Chat);

    //emit TcpMgr::Getinstance()->sig_swich_chatdlg();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::slot_switch_Register()
{
    _RegisterDialog = new RegisterDialog(this);
    _RegisterDialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);

    setCentralWidget(_RegisterDialog);
    _loginDialog->hide();
    _RegisterDialog->show();
    connect(_RegisterDialog, &RegisterDialog::sig_retrun_login, this, &MainWindow::slot_switch_Login);


}

void MainWindow::slot_switch_Login()
{
    _loginDialog = new LoginDialog(this);
    _loginDialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_loginDialog);
    _RegisterDialog->hide();
    _loginDialog->show();
    connect(_loginDialog, &LoginDialog::switch_RegisterDialog, this, &MainWindow::slot_switch_Register);
	connect(_loginDialog, &LoginDialog::sig_switch_Reset, this, &MainWindow::slot_switch_Reset);
}

void MainWindow::slot_switch_Reset()
{
	_resetDialog = new ResetDialog(this);
	_resetDialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);

	setCentralWidget(_resetDialog);
	_loginDialog->hide();
    _resetDialog->show();

    connect(_resetDialog, &ResetDialog::switchLogin, this, &MainWindow::slot_switch_Login_2);
}

void MainWindow::slot_switch_Login_2()
{
    _loginDialog = new LoginDialog(this);
    _loginDialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(_loginDialog);
    _loginDialog->hide();
    _loginDialog->show();
    //连接登录界面忘记密码信号
    connect(_loginDialog, &LoginDialog::sig_switch_Reset, this, &MainWindow::slot_switch_Reset);
    //连接登录界面注册信号
    connect(_loginDialog, &LoginDialog::switch_RegisterDialog, this, &MainWindow::slot_switch_Register);
}

void MainWindow::slot_switch_Chat()
{
	_chatDialog = new ChatDialog(this);
	_chatDialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);

	setCentralWidget(_chatDialog);
	_loginDialog->hide();
	_chatDialog->show();
    this->setMinimumSize(QSize(1050, 900));
    this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

}
