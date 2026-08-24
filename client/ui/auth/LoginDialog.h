#pragma once

#include <QDialog>
#include "ui_LoginDialog.h"
#include <QAction>
#include <QPainter>
#include "httpmgr.h"
QT_BEGIN_NAMESPACE
namespace Ui { class LoginDialogClass; };
QT_END_NAMESPACE

class LoginDialog : public QDialog
{
	Q_OBJECT

public:
	LoginDialog(QWidget *parent = nullptr);
	~LoginDialog();
public slots:
	void slot_forget_pwd();
	void togglePasswordVisibility();
	void slot_login_btn();
	void slot_login_mod_finish(Req id, QString res, ErrorCode err);
	void slot_tcp_con_finish(bool bsuccess);
	void slot_login_failed(int err);
protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
private:
	void initHead();
	void updateHeadPixmap();
	void initHttpHandlers();
	bool checkEmailValid();
	bool checkPassValid();
	void AddTipErr(TipErr te, QString tips);
	void DelTipErr(TipErr te);
	void showTip(QString  str, bool b_ok);
	void Enablebtn(bool enabled);
	Ui::LoginDialogClass* ui;
	QMap<TipErr, QString> _tip_errs;
	QMap<Req, std::function<void(const QJsonObject&)>> _handlers;
	QAction* toggleAction;
	QString _token;
	int _uid;

signals:
	void switch_RegisterDialog();
	void sig_switch_Reset();
	void sig_connect_tcp(ServerInfo info);
};
