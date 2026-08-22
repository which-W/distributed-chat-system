#pragma once

#include <QDialog>
#include "ui_Loadingdlg.h"

QT_BEGIN_NAMESPACE
namespace Ui { class LoadingdlgClass; };
QT_END_NAMESPACE

class Loadingdlg : public QDialog
{
	Q_OBJECT

public:
	Loadingdlg(QWidget *parent = nullptr);
	~Loadingdlg();

private:
	Ui::LoadingdlgClass *ui;
};
