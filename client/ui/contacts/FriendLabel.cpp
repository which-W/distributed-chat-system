#include "FriendLabel.h"

FriendLabel::FriendLabel(QWidget* parent)
	: QFrame(parent)
	, ui(new Ui::FriendLabelClass())
{
	ui->setupUi(this);
	ui->close_lb->SetState("normal", "hover", "pressed",
		"selected_normal", "selected_hover", "selected_pressed");
	connect(ui->close_lb, &ClickLabel::clicked, this, &FriendLabel::slot_close);
}

FriendLabel::~FriendLabel()
{
	delete ui;
}

void FriendLabel::SetText(QString text)
{
    _text = text;
    ui->tip_label->setText(_text);
    ui->tip_label->adjustSize();

    QFontMetrics fontMetrics(ui->tip_label->font()); // 获取QLabel控件的字体信息
    auto textWidth = fontMetrics.width(ui->tip_label->text()); // 获取文本的宽度
    auto textHeight = fontMetrics.height(); // 获取文本的高度
    this->setFixedWidth(ui->tip_label->width() + ui->close_lb->width() + 5);
    this->setFixedHeight(textHeight + 2);
    _width = this->width();
    _height = this->height();
}

int FriendLabel::Width()
{
    return _width;
}

int FriendLabel::Height()
{
    return _height;
}

QString FriendLabel::Text()
{
    return _text;
}

void FriendLabel::slot_close()
{
    emit sig_close(_text);
}
