#include "ResetDialog.h"
#include "ElaMessageBar.h"

ResetDialog::ResetDialog(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::ResetDialogClass()), _counter(5)
{
	ui->setupUi(this);
	// 重置页与登录、注册页保持一致，默认隐藏新密码，防止肩窥和录屏泄露。
	ui->pwd_edit->setEchoMode(QLineEdit::Password);
	ui->err_tip->hide();
   connect(ui->user_edit , &QLineEdit::editingFinished, this, [this]() {
	   checkUserValid();
	   });
   connect(ui->email_edit, &QLineEdit::editingFinished, this, [this]() {
       checkEmailValid();
       });

   connect(ui->pwd_edit, &QLineEdit::editingFinished, this, [this]() {
       checkPassValid();
       });
   connect(ui->varify_edit, &QLineEdit::editingFinished, this, [this]() {
       checkVarifyValid();
       });
   connect(ui->return_btn, &QPushButton::clicked, this, &ResetDialog::on_return_btn_clicked);
   connect(ui->get_code, &QPushButton::clicked, this, &ResetDialog::on_varify_btn_clicked);
   //连接reset相关信号和注册处理回调
   initHandlers();
   connect(Httpmgr::Getinstance().get(), &Httpmgr::sig_reset_mod_finish, this,
	   &ResetDialog::slot_reset_mod_finish);

   //返回页面逻辑
   _timer = new QTimer(this);

}

ResetDialog::~ResetDialog()
{
	delete ui;
}

void ResetDialog::on_return_btn_clicked()
{
	_timer->stop();
	qDebug() << "sure btn clicked ";
	emit switchLogin();
}

void ResetDialog::on_varify_btn_clicked()
{
	//获取验证码按钮点击事件
	auto email = ui->email_edit->text();
	if (email.isEmpty()) {
		showTip(tr("邮箱不能为空"), false);
		return;
	}
	QRegularExpression regex("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
	bool match = regex.match(email).hasMatch();
	if (match) {
		//发送http获取验证码请求
		QJsonObject json_obj;
		json_obj["email"] = email;
		Httpmgr::Getinstance()->PostHttpRequest(gate_url_prefix + "/post_email",
			json_obj, Req::ID_GET_VERIFT_CODE, Modules::RESETMOD);
	}
}

void ResetDialog::slot_reset_mod_finish(Req id, QString res, ErrorCode err)
{
	if (err != ErrorCode::ERR_OK) {
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

void ResetDialog::on_sure_btn_clicked()
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

	valid = checkVarifyValid();
	if (!valid) {
		return;
	}

	//发送http重置用户请求
	QJsonObject json_obj;
	json_obj["user"] = ui->user_edit->text();
	json_obj["email"] = ui->email_edit->text();
	json_obj["passwd"] = ui->pwd_edit->text();
	json_obj["varifycode"] = ui->varify_edit->text();
	Httpmgr::Getinstance()->PostHttpRequest(gate_url_prefix + "/reset_pwd",
		json_obj, Req::ID_RESET_PWD, Modules::RESETMOD);
}

bool ResetDialog::checkUserValid()
{
	if (ui->user_edit->text() == "") {
		AddTipErr(TipErr::TIP_USER_ERR, tr("用户名不能为空"));
		return false;
	}

	DelTipErr(TipErr::TIP_USER_ERR);
	return true;

}

bool ResetDialog::checkPassValid()
{
	auto pass = ui->pwd_edit->text();

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

void ResetDialog::showTip(QString str, bool b_ok)
{
	if (isVisible()) {
		if (b_ok) {
			ElaMessageBar::success(ElaMessageBarType::TopRight, tr("成功"), str, 2600, window());
		} else {
			ElaMessageBar::error(ElaMessageBarType::TopRight, tr("请检查输入"), str, 3200, window());
		}
	}
}

bool ResetDialog::checkEmailValid()
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

bool ResetDialog::checkVarifyValid()
{
	auto pass = ui->varify_edit->text();
	if (pass.isEmpty()) {
		AddTipErr(TipErr::TIP_VARIFY_ERR, tr("验证码不能为空"));
		return false;
	}

	DelTipErr(TipErr::TIP_VARIFY_ERR);
	return true;
}

void ResetDialog::AddTipErr(TipErr te, QString tips)
{
	_tip_errs[te] = tips;
	showTip(tips, false);
}

void ResetDialog::DelTipErr(TipErr te)
{
	_tip_errs.remove(te);
}

void ResetDialog::initHandlers()
{
	//注册获取验证码回包逻辑
	_handlers.insert(Req::ID_GET_VERIFT_CODE, [this](QJsonObject jsonObj) {
		int error = jsonObj["error"].toInt();
		if (error != ErrorCode::ERR_OK) {
			showTip(tr("参数错误"), false);
			return;
		}
		auto email = jsonObj["email"].toString();
		showTip(tr("验证码已发送到邮箱，注意查收"), true);
		});

	//注册注册用户回包逻辑
	_handlers.insert(Req::ID_RESET_PWD, [this](QJsonObject jsonObj) {
		int error = jsonObj["error"].toInt();
		if (error != ErrorCode::ERR_OK) {
			showTip(tr("参数错误"), false);
			return;
		}
		auto email = jsonObj["email"].toString();
		showTip(tr("重置成功,点击返回登录"), true);
		});
}
