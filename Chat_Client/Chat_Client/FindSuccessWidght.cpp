#include "FindSuccessWidght.h"

FindSuccessWidght::FindSuccessWidght(QWidget *parent)
	: QDialog(parent),_parent(parent)
	, ui(new Ui::FindSuccessWidghtClass())
{
	ui->setupUi(this);
	codec = QTextCodec::codecForName("GBK");
    // 设置对话框标题
    setWindowTitle(codec->toUnicode("添加"));
    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setObjectName("FindSuccessWidghtClass");
    // 获取当前应用程序的路径
    QString app_path = QCoreApplication::applicationDirPath();
    QString pix_path = QDir::toNativeSeparators(app_path +
        QDir::separator() + "static" + QDir::separator() + "head_1.jpg");
    QPixmap head_pix(pix_path);
    head_pix = head_pix.scaled(ui->head_lb->size(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->head_lb->setPixmap(head_pix);
    ui->add_friend_btn->SetState("normal", "hover", "press");
    this->setModal(true);
}

FindSuccessWidght::~FindSuccessWidght()
{
	delete ui;
}

void FindSuccessWidght::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
    ui->name_lb->setText(si->_name);
    _si = si;
}

void FindSuccessWidght::on_add_friend_btn_clicked()
{
    this->hide();
    auto applyfriend = new ApplyFriend(_parent);
    // 显示前先调整大小，然后居中
    applyfriend->adjustSize();  // 确保对话框有正确的尺寸
    // 居中显示
    QRect parentRect = this->geometry();
    int x = parentRect.x() + (parentRect.width() - applyfriend->width()) / 2;
    int y = parentRect.y() + (parentRect.height() - applyfriend->height()) / 2;
    applyfriend->move(x, y);
    applyfriend->SetSearchInfo(_si);
    applyfriend->setModal(true);
    applyfriend->show();
}
