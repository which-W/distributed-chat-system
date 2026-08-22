#include "Loadingdlg.h"
#include <QMovie>
Loadingdlg::Loadingdlg(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::LoadingdlgClass())
{
	ui->setupUi(this);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground); // 设置背景透明
    ui->loading_lb->setScaledContents(true);     // 让 QLabel 自己缩放显示
    ui->loading_lb->setFixedSize(200, 200);      // 设置 QLabel 大小

    QMovie* movie = new QMovie(":/res/loading.gif"); // 加载动画的资源文件
    ui->loading_lb->setMovie(movie);
    movie->start();
}

Loadingdlg::~Loadingdlg()
{
	delete ui;
}
