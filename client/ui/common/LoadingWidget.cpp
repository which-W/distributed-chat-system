#include "LoadingWidget.h"

LoadingWidget::LoadingWidget(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::LoadingWidgetClass())
{
	ui->setupUi(this);

    QMovie* movie = new QMovie(":/res/loading.gif"); // 加载动画的资源文件
    ui->loading_lb->setMovie(movie);
    ui->loading_lb->setScaledContents(true);     // 让 QLabel 自己缩放显示
    ui->loading_lb->setFixedSize(50, 50);      // 设置 QLabel 大小
    movie->start();
}

LoadingWidget::~LoadingWidget()
{
	delete ui;
}
