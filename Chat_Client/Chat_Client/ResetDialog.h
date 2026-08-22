#pragma once

#include <QDialog>
#include "ui_ResetDialog.h"
#include "global.h"
#include "HttpMgr.h"
#include <QTimer>
QT_BEGIN_NAMESPACE
namespace Ui { class ResetDialogClass; };
QT_END_NAMESPACE

class ResetDialog : public QDialog
{
	Q_OBJECT

public:
	ResetDialog(QWidget *parent = nullptr);
	~ResetDialog();

private slots:
    void on_return_btn_clicked();
    void on_varify_btn_clicked();
    void slot_reset_mod_finish(Req id, QString res, ErrorCode err);
    void on_sure_btn_clicked();

private:
    bool checkUserValid();
    bool checkPassValid();
    void showTip(QString str, bool b_ok);
    bool checkEmailValid();
    bool checkVarifyValid();
    void AddTipErr(TipErr te, QString tips);
    void DelTipErr(TipErr te);
    void initHandlers();
    Ui::ResetDialogClass* ui;
    QMap<TipErr, QString> _tip_errs;
    QMap<Req, std::function<void(const QJsonObject&)>> _handlers;
    QTextCodec* codec;
	QTimer* _timer;
    int _counter;
signals:
    void switchLogin();
};
