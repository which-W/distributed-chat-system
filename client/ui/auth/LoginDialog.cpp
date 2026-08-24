#include "LoginDialog.h"
#include <QEvent>
#include <QPainterPath>
#include <QTimer>
#include "ElaMessageBar.h"
#include "ClickLabel.h"
#include "TcpMgr.h"
LoginDialog::LoginDialog(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::LoginDialogClass())
{
	ui->setupUi(this);
	ui->err_tip->hide();
	ui->psw_line_edit->setEchoMode(QLineEdit::Password);

	toggleAction = ui->psw_line_edit->addAction(QIcon(":/res/close.png"), QLineEdit::TrailingPosition);
	connect(toggleAction, &QAction::triggered, this, &LoginDialog::togglePasswordVisibility);
	connect(ui->exit_button, &QPushButton::clicked, this, &LoginDialog::switch_RegisterDialog);
	connect(ui->login_button, &QPushButton::clicked, this, &LoginDialog::slot_login_btn);
	connect(ui->user_line_edit, &QLineEdit::editingFinished, this, [this]() {
		checkEmailValid();
		});
	connect(ui->psw_line_edit, &QLineEdit::editingFinished, this, [this]() {
		checkPassValid();
		});
	ui->forget_label->SetState("normal", "hover", "", "selected", "selected_hover", "");
	ui->forget_label->setCursor(Qt::PointingHandCursor);
	connect(ui->forget_label, &ClickLabel::clicked, this, &LoginDialog::slot_forget_pwd);

	initHttpHandlers();
	initHead();

	//连接登录回包信号
	connect(Httpmgr::Getinstance().get(), &Httpmgr::sig_login_finish,this,
		&LoginDialog::slot_login_mod_finish);
	//连接tcpMgr的信号
	connect(this, &LoginDialog::sig_connect_tcp, TcpMgr::Getinstance().get(), &TcpMgr::slot_tcp_connect);
	connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_con_success, this, &LoginDialog::slot_tcp_con_finish);
	connect(TcpMgr::Getinstance().get(), &TcpMgr::sig_login_failed, this, &LoginDialog::slot_login_failed);
}

LoginDialog::~LoginDialog()
{
	delete ui;
}

void LoginDialog::slot_forget_pwd()
{
	qDebug() << "Forget password clicked";
	emit sig_switch_Reset();
}

void LoginDialog::togglePasswordVisibility()
{
	if (ui->psw_line_edit->echoMode() == QLineEdit::Password) {
		ui->psw_line_edit->setEchoMode(QLineEdit::Normal);
		toggleAction->setIcon(QIcon(":/res/open.png"));
	}
	else {
		ui->psw_line_edit->setEchoMode(QLineEdit::Password);
		toggleAction->setIcon(QIcon(":/res/close.png"));
	}
}

void LoginDialog::slot_login_btn()
{
	qDebug() << "login button clicked";
	if (checkEmailValid() == false) {
		return;
	}
	if (checkPassValid() == false) {
		return;
	}

	Enablebtn(false);
	auto email = ui->user_line_edit->text();
	auto psw = xosString(ui->psw_line_edit->text());

	QJsonObject jsonobj;
	jsonobj["email"] = email;
	jsonobj["passwd"] = psw;
	Httpmgr::Getinstance()->PostHttpRequest(QUrl(gate_url_prefix + "/user_login").toString(),
		jsonobj, Req::ID_LOGIN_USER, Modules::LODINMOD);

}

void LoginDialog::slot_login_mod_finish(Req id, QString res, ErrorCode err)
{
	if (err != ErrorCode::ERR_OK){
		showTip(tr("网络请求错误"), false);
		return;
	}
	// 解析 JSON 字符串,res需转化为QByteArray
	QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
	//json解析错误
	if (jsonDoc.isNull()) {
		showTip(tr("json解析错误"), false);
		return;
	}
	//json解析错误
	if (!jsonDoc.isObject()) {
		showTip(tr("json解析错误"), false);
		return;
	}
	//调用对应的逻辑,根据id回调。
	_handlers[id](jsonDoc.object());
	return;
}

void LoginDialog::slot_tcp_con_finish(bool bsuccess)
{
	if (bsuccess) {
		showTip(tr("聊天服务连接成功，正在登录..."), true);
		QJsonObject jsonObj;
		jsonObj["uid"] = _uid;
		jsonObj["token"] = _token;
		QJsonDocument doc(jsonObj);
		QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
		//发送tcp请求给chat server
		emit TcpMgr::Getinstance()->sig_send_data(Req::ID_CHAT_LOGIN, jsonData);
	}
	else {
		showTip(tr("网络异常"), false);
		Enablebtn(true);
	}
}

void LoginDialog::slot_login_failed(int err)
{
	qDebug() << "Login failed, error code: " << err;
	switch (err) {
	case ErrorCode::ERR_FAIL:
		showTip(tr("登录失败，请检查用户名或密码"), false);
		break;
	case ErrorCode::ERR_NETWORK:
		showTip(tr("网络异常，请稍后再试"), false);
		break;
	case ErrorCode::ERR_JSON:
		showTip(tr("服务器返回数据异常"), false);
		break;
	default:
		showTip(tr("未知错误，请稍后再试"), false);
		break;
	}
	Enablebtn(true);
}

void LoginDialog::initHttpHandlers()
{
	//注册获取登录回包逻辑
	_handlers.insert(Req::ID_LOGIN_USER, [this](QJsonObject jsonObj) {
		int error = jsonObj["error"].toInt();
		if (error != ErrorCode::ERR_OK){
			showTip(tr("账号或者密码错误"), false);
			Enablebtn(true);
			return;
		}
		auto user = jsonObj["user"].toString();
		//发送信号通知tcpMgr发送长链接
		ServerInfo si;
		si.Uid = jsonObj["uid"].toInt();
		si.Host = jsonObj["host"].toString();
		si.Port = jsonObj["port"].toString();
		si.Token = jsonObj["token"].toString();
		si.Transport = jsonObj["transport"].toString("insecure").toLower();
		si.TlsServerName = jsonObj["tls_server_name"].toString();
		si.AllowInsecure = allow_insecure_transport;
		_uid = si.Uid;
		_token = si.Token;
		emit sig_connect_tcp(si);
		});
}

void LoginDialog::initHead()
{
	ui->label->setAlignment(Qt::AlignCenter);
	ui->label->setMinimumSize(120, 120);
	ui->label->setMaximumSize(168, 168);
	ui->label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	ui->label->installEventFilter(this);
	QTimer::singleShot(0, this, &LoginDialog::updateHeadPixmap);
}

void LoginDialog::updateHeadPixmap()
{
	QPixmap originalPixmap(":/res/R-C.png");
	const QSize available = ui->label->contentsRect().size();
	if (originalPixmap.isNull() || available.isEmpty()) {
		return;
	}
	originalPixmap = originalPixmap.scaled(available, Qt::KeepAspectRatio,
		Qt::SmoothTransformation);

	// 创建一个和原始图片相同大小的QPixmap，用于绘制圆角图片
	QPixmap roundedPixmap(originalPixmap.size());
	roundedPixmap.fill(Qt::transparent); // 用透明色填充

	QPainter painter(&roundedPixmap);
	painter.setRenderHint(QPainter::Antialiasing); // 设置抗锯齿，使圆角更平滑
	painter.setRenderHint(QPainter::SmoothPixmapTransform);

	// 使用QPainterPath设置圆角
	QPainterPath path;
	path.addRoundedRect(0, 0, originalPixmap.width(), originalPixmap.height(), 16, 16);
	painter.setClipPath(path);

	// 将原始图片绘制到roundedPixmap上
	painter.drawPixmap(0, 0, originalPixmap);

	// 设置绘制好的圆角图片到QLabel上
	ui->label->setPixmap(roundedPixmap);
}

bool LoginDialog::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == ui->label && event->type() == QEvent::Resize) {
		updateHeadPixmap();
	}
	return QDialog::eventFilter(watched, event);
}

bool LoginDialog::checkEmailValid()
{
	//验证邮箱的地址正则表达式
	auto email = ui->user_line_edit->text();
	// 邮箱地址的正则表达式
	QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
	bool match = regex.match(email).hasMatch(); // 执行正则表达式匹配
	if (!match) {
		//提示邮箱不正确
		AddTipErr(TipErr::TIP_EMAIL_ERR, tr("邮箱地址不正确"));
		return false;
	}

	DelTipErr(TipErr::TIP_EMAIL_ERR);
	return true;
}

bool LoginDialog::checkPassValid()
{
	auto pass = ui->psw_line_edit->text();

	if (pass.length() < 6 || pass.length() > 15) {
		//提示长度不准确
		AddTipErr(TipErr::TIP_PWD_ERR, tr("密码长度应为6~15"));
		return false;
	}

	// 创建一个正则表达式对象，按照上述密码要求
	// 这个正则表达式解释：
	// ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
	QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*.]{6,15}$");
	bool match = regExp.match(pass).hasMatch();
	if (!match) {
		//提示字符非法
		AddTipErr(TipErr::TIP_PWD_ERR, tr("不能包含非法字符"));
		return false;;
	}

	DelTipErr(TipErr::TIP_PWD_ERR);

	return true;
}

void LoginDialog::AddTipErr(TipErr te, QString tips)
{
	_tip_errs[te] = tips;
	showTip(tips, false);
}

void LoginDialog::DelTipErr(TipErr te)
{
	_tip_errs.remove(te);
}

void LoginDialog::showTip(QString  str, bool b_ok)
{
	if (isVisible()) {
		if (b_ok) {
			ElaMessageBar::success(ElaMessageBarType::TopRight, tr("成功"), str, 2600, window());
		} else {
			ElaMessageBar::error(ElaMessageBarType::TopRight, tr("请检查输入"), str, 3200, window());
		}
	}
}

void LoginDialog::Enablebtn(bool enabled)
{
	ui->login_button->setEnabled(enabled);
}
