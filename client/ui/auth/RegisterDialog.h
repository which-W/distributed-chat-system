#pragma once
#include <QDialog>
#include "ui_RegisterDialog.h"
#include "global.h"
#include <QTimer>
#include <QRandomGenerator>
QT_BEGIN_NAMESPACE
namespace Ui { class RegisterDialogClass; };
QT_END_NAMESPACE

class RegisterDialog : public QDialog
{
	Q_OBJECT

public:
	RegisterDialog(QWidget *parent = nullptr);
	~RegisterDialog();
	void showTip(QString str, bool b_ok);
	void initHandlers();
public slots:
	void get_code_func();
	void slot_req_mod_finished(Req id, QString res, ErrorCode error);
	void slot_reg_finished();
	void togglePasswordVisibility();
	void togglePasswordconfirmVisibility();
	void changeRegisterWidgepage();
	void on_return_btn_clicked();
	void on_return_cansel_btn_clicked();
signals:
	void sig_retrun_login();
private:
	Ui::RegisterDialogClass *ui;
	bool checkUserValid();
	bool checkEmailValid();
	bool checkPassValid();
	bool checkConfirmValid();
	bool checkVarifyValid();
	void AddTipErr(TipErr te, QString tips);
	void DelTipErr(TipErr te);

	QMap<Req, std::function<void(const QJsonObject&)>> _handlers;
	QMap<TipErr, QString> _tip_errs;
	QAction* toggleAction;
	QAction* toggleAction2;
	QTimer* _timer;
	int _counter;


};
