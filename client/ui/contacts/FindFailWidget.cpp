#include "FindFailWidget.h"

FindFailWidget::FindFailWidget(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::FindFailWidgetClass())
{
	ui->setupUi(this);
	setWindowTitle("添加");
    // 设置对话框标题
    setWindowTitle("添加");
    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setObjectName("FindFailWidgetClass");
    ui->fail_sure_btn->SetState("normal", "hover", "press");
    // 获取当前应用程序的路径
    this->setModal(true);
	connect(ui->fail_sure_btn, &ClickedBtn::clicked, this, &FindFailWidget::fail_sure_btn_clicked);
}

FindFailWidget::~FindFailWidget()
{
	delete ui;
}

void FindFailWidget::fail_sure_btn_clicked()
{
    this->hide();
}
