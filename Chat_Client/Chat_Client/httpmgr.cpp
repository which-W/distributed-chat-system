#include "httpmgr.h"


Httpmgr::Httpmgr()
{
	// 连接信号槽
	connect(this, &Httpmgr::sig_http_finish, this, &Httpmgr::slot_http_finished);
}


Httpmgr::~Httpmgr()
{
	// 清理资源
	_manager.deleteLater();
}

void Httpmgr::PostHttpRequest(const QString& url, const QJsonObject& jsonObj, Req req_id, Modules mod)
{
	// 将 QJsonObject 转换为 QByteArray 并设置请求头
	QByteArray postData = QJsonDocument(jsonObj).toJson();
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(postData.length()));
	auto self = shared_from_this();
	QNetworkReply* reply = _manager.post(request, postData);
	// 连接信号槽处理网络请求完成
	connect(reply, &QNetworkReply::finished, this, [self, req_id, mod, reply]() {
		if (reply->error() == QNetworkReply::NoError) {
			QString response = reply->readAll();
			emit self->sig_http_finish(req_id, response, ERR_OK, mod);

		}
		else {
			qDebug() << "HTTP request failed:" << reply->errorString();
			emit self->sig_http_finish(req_id, QString(), ERR_NETWORK, mod);
		}
		reply->deleteLater();
		return;
		});
}


void Httpmgr::slot_http_finished(Req id, QString res, ErrorCode error, Modules mod)
{
	// 处理 HTTP 请求完成的逻辑
	if (mod == Modules::MOD_REGISTER) {
		if (error == ERR_OK) {
			// 处理注册成功的逻辑
			qDebug() << "Registration successful:" << res;
			emit sig_reg_mod_finish(id, res, error);
		}
		else {
			// 处理注册失败的逻辑
			qDebug() << "Registration failed with error code:" << error;
		}
	}
	else {
		qDebug() << "Unhandled module in slot_http_finished:" << mod;
	}

	if (mod == Modules::RESETMOD) {
		if (error == ErrorCode::ERR_OK) {
			// 处理重置成功的逻辑
			qDebug() << "Reset successful:" << res;
			emit sig_reset_mod_finish(id, res, error);
		}
		else {
			// 处理重置失败的逻辑
			qDebug() << "Reset failed with error code:" << error;
		}
	}
	else {
		qDebug() << "Unhandled module in slot_http_finished:" << mod;
	}

	if (mod == Modules::LODINMOD) {
		if (error == ErrorCode::ERR_OK) {
			qDebug() << "Login successful";
			emit sig_login_finish(id, res, error);
		}
		else {
			// 处理重置失败的逻辑
			qDebug() << "Login fail error code:" << error;
		}
	}
	else {
		qDebug() << "Unhandled module in slot_http_finished:" << mod;
	}
}
