#include "RegisterDialog.h"
#include "global.h"
#include "httpmgr.h"
#include <QUrl>
#include <QAction>
#include <QPixmap>
#include "ElaMessageBar.h"
RegisterDialog::RegisterDialog(QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::RegisterDialogClass()),_counter(5)
{
	initHandlers();
	//页面初始化逻辑
	ui->setupUi(this);
	ui->psw_edit->setEchoMode(QLineEdit::Password);
	ui->psw_edit_2->setEchoMode(QLineEdit::Password);
	toggleAction = ui->psw_edit->addAction(QIcon(":/res/close.png"), QLineEdit::TrailingPosition);
	toggleAction2 = ui->psw_edit_2->addAction(QIcon(":/res/close.png"), QLineEdit::TrailingPosition);
	connect(toggleAction, &QAction::triggered, this, &RegisterDialog::togglePasswordVisibility);
	connect(toggleAction2, &QAction::triggered, this, &RegisterDialog::togglePasswordconfirmVisibility);
	ui->err_tip->hide();
	//消息发送逻辑
	connect(ui->get_code, &QPushButton::clicked, this, &RegisterDialog::get_code_func);
	connect(Httpmgr::Getinstance().get(), &Httpmgr::sig_reg_mod_finish, this, &RegisterDialog::slot_req_mod_finished);
	connect(ui->sure_btn, &QPushButton::clicked, this, &RegisterDialog::slot_reg_finished);
	connect(ui->return_log, &QPushButton::clicked, this, &RegisterDialog::on_return_btn_clicked);
	connect(ui->cansel_btn, &QPushButton::clicked, this, &RegisterDialog::on_return_cansel_btn_clicked);
	//输入框检测逻辑
	connect(ui->user_edit, &QLineEdit::editingFinished, this, [this]() {
		checkUserValid();
		});
	connect(ui->email_edit, &QLineEdit::editingFinished, this, [this]() {
		checkEmailValid();
		});
	connect(ui->psw_edit, &QLineEdit::editingFinished, this, [this]() {
		checkPassValid();
		});
	connect(ui->psw_edit_2, &QLineEdit::editingFinished, this, [this]() {
		checkConfirmValid();
		});
	connect(ui->code_edit, &QLineEdit::editingFinished, this, [this]() {
		checkVarifyValid();
		});

	//返回页面逻辑
	_timer = new QTimer(this);

	connect(_timer, &QTimer::timeout, [this]() {
		if (_counter == 0) {
			_timer->stop();
			emit sig_retrun_login();
			return;
		}
		_counter--;
		auto str = tr("在 %1 s后将返回登录页面").arg(_counter);
		ui->show_tip->setText(str);
		});

}

void RegisterDialog::get_code_func() {
	auto email = ui->email_edit->text();
	QRegularExpression regex("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
	bool match = regex.match(email).hasMatch();
	if (match) {
		QJsonObject json;
		json["email"] = email;
		Httpmgr::Getinstance()->PostHttpRequest(gate_url_prefix + "/post_email", json, Req::ID_GET_VERIFT_CODE, Modules::MOD_REGISTER);
	}
	else {
		showTip(tr("邮箱输入错误"),false);
	}

}

RegisterDialog::~RegisterDialog()
{
	delete ui;
}

void RegisterDialog::showTip(QString  str ,bool b_ok)
{
	if (isVisible()) {
		if (b_ok) {
			ElaMessageBar::success(ElaMessageBarType::TopRight, tr("成功"), str, 2600, window());
		} else {
			ElaMessageBar::error(ElaMessageBarType::TopRight, tr("请检查输入"), str, 3200, window());
		}
	}
}

void RegisterDialog::initHandlers()
{
	_handlers.insert(ID_GET_VERIFT_CODE, [this](const QJsonObject& jsonObj) {
		if (jsonObj.contains("error") && jsonObj["error"].toInt() == ErrorCode::ERR_OK) {
			showTip(tr("注册成功"), true);
		}
		else {
			showTip(tr("未知参数失败，请重试"), false);
		}
		auto emial = jsonObj["email"].toString();
		showTip(tr("验证码已发送到邮箱") , true);

		});

	_handlers.insert(Req::ID_REQ_USER, [this](QJsonObject jsonObj) {
		int error = jsonObj["error"].toInt();
		if (error != ErrorCode::ERR_OK) {
			showTip(tr("用户或密码错误"), false);
			return;
		}
		auto email = jsonObj["email"].toString();
		showTip(tr("用户注册成功"), true);
		changeRegisterWidgepage();
		});
}

void RegisterDialog::slot_req_mod_finished(Req id, QString res, ErrorCode error)
{
	if (error == ERR_OK) {
		showTip(tr("注册成功"), true);
	}
	else {
		showTip(tr("网络请求错误"), false);
		return;
	}
	//解析json数据
	QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
	if (jsonDoc.isNull() || !jsonDoc.isObject()) {
		showTip(tr("服务器返回数据错误"), false);
		return;
	}
	//创建json对象,并调用对应的处理函数
	_handlers[id](jsonDoc.object());
	return;
}

void RegisterDialog::slot_reg_finished()
{
	bool valid = checkUserValid();
	if (!valid) {
		return;
	}
	valid = checkEmailValid();
	if (!valid) {
		return;
	}
	valid = checkPassValid();
	if (!valid) {
		return;
	}
	valid = checkConfirmValid();
	if (!valid) {
		return;
	}
	valid = checkVarifyValid();
	if (!valid) {
		return;
	}

	//发送http请求注册用户
	QJsonObject json_obj;
	json_obj["user"] = ui->user_edit->text();
	json_obj["email"] = ui->email_edit->text();
	// HTTPS 仅负责传输保护；口令强度校验和 Argon2id 哈希统一由服务端执行。
	json_obj["passwd"] = ui->psw_edit->text();
	json_obj["sex"] = 0;

	int randomValue = QRandomGenerator::global()->bounded(100); // 生成0到99之间的随机整数
	int head_i = randomValue % heads.size();

	json_obj["icon"] = heads[head_i];
	json_obj["nick"] = ui->user_edit->text();
	json_obj["confirm"] = ui->psw_edit_2->text();
	json_obj["varifycode"] = ui->code_edit->text();
	Httpmgr::Getinstance()->PostHttpRequest(gate_url_prefix + "/user_register",
		json_obj, Req::ID_REQ_USER, Modules::MOD_REGISTER);
}

void RegisterDialog::togglePasswordVisibility()
{
	if (ui->psw_edit->echoMode() == QLineEdit::Password) {
		ui->psw_edit->setEchoMode(QLineEdit::Normal);
		toggleAction->setIcon(QIcon(":/res/open.png"));
	}
	else {
		ui->psw_edit->setEchoMode(QLineEdit::Password);
		toggleAction->setIcon(QIcon(":/res/close.png"));
	}
}

void RegisterDialog::togglePasswordconfirmVisibility()
{

	if (ui->psw_edit_2->echoMode() == QLineEdit::Password) {
		ui->psw_edit_2->setEchoMode(QLineEdit::Normal);
		toggleAction2->setIcon(QIcon(":/res/open.png"));
	}
	else {
		ui->psw_edit_2->setEchoMode(QLineEdit::Password);
		toggleAction2->setIcon(QIcon(":/res/close.png"));
	}
}

void RegisterDialog::changeRegisterWidgepage()
{
	_timer->stop();
	ui->stackedWidget->setCurrentWidget(ui->page2);
	_timer->start(1000);
}

void RegisterDialog::on_return_btn_clicked()
{
	_timer->stop();
	emit sig_retrun_login();
}

void RegisterDialog::on_return_cansel_btn_clicked()
{
	_timer->stop();
	emit sig_retrun_login();
}

void RegisterDialog::AddTipErr(TipErr te, QString tips)
{
	_tip_errs[te] = tips;
	showTip(tips, false);
}

void RegisterDialog::DelTipErr(TipErr te)
{
	_tip_errs.remove(te);
}

bool RegisterDialog::checkUserValid()
{
	if (ui->user_edit->text() == "") {
		AddTipErr(TipErr::TIP_USER_ERR, tr("用户名不能为空"));
		return false;
	}
	DelTipErr(TipErr::TIP_USER_ERR);
	return true;
}

bool RegisterDialog::checkPassValid()
{
	auto pass = ui->psw_edit->text();
	auto pass_confirm = ui->psw_edit_2->text();
	// QString::length 按 UTF-16 单元计数；使用 Unicode 码点数与服务端策略保持一致。
	const auto passwordCodePoints = pass.toUcs4().size();
	if (passwordCodePoints < 10 || passwordCodePoints > 128) {
		//提示长度不准确
		AddTipErr(TipErr::TIP_PWD_ERR, tr("密码长度应为10~128"));
		return false;
	}
	// 创建一个正则表达式对象，按照上述密码要求
	// 这个正则表达式解释：
	// ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
	QRegularExpression regExp("^[^\\s]{10,128}$");
	bool match = regExp.match(pass).hasMatch();
	if (!match) {
		//提示字符非法
		AddTipErr(TipErr::TIP_PWD_ERR, tr("不能包含非法字符"));
		return false;;
	}
	DelTipErr(TipErr::TIP_PWD_ERR);
	return true;
}

bool RegisterDialog::checkConfirmValid()
{
	auto pass = ui->psw_edit->text();
	auto pass_con = ui->psw_edit_2->text();

	if (pass != pass_con) {
		AddTipErr(TipErr::TIP_PWD_CONFIRM, tr("密码第二次输入错误"));
		return false;
	}
	else {
		DelTipErr(TipErr::TIP_PWD_CONFIRM);
	}
	return true;
}

bool RegisterDialog::checkEmailValid()
{
	//验证邮箱的地址正则表达式
	auto email = ui->email_edit->text();
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

bool RegisterDialog::checkVarifyValid()
{
	auto pass = ui->code_edit->text();
	if (pass.isEmpty()) {
		AddTipErr(TipErr::TIP_VARIFY_ERR, tr("验证码不能为空"));
		return false;
	}
	DelTipErr(TipErr::TIP_VARIFY_ERR);
	return true;
}
