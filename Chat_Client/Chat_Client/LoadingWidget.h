#pragma once

#include <QWidget>
#include "ui_LoadingWidget.h"
#include <QMovie>
QT_BEGIN_NAMESPACE
namespace Ui { class LoadingWidgetClass; };
QT_END_NAMESPACE

class LoadingWidget : public QWidget
{
	Q_OBJECT

public:
	LoadingWidget(QWidget *parent = nullptr);
	~LoadingWidget();

private:
	Ui::LoadingWidgetClass *ui;
};
